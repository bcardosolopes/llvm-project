//===---- CIRGenBuiltinAArch64.cpp - Emit CIR for AArch64 builtins --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit ARM64 Builtin calls as CIR or a function call
// to be later resolved.
//
//===----------------------------------------------------------------------===//

#include <utility>

#include "CIRGenBuilder.h"
#include "CIRGenCXXABI.h"
#include "CIRGenCall.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/MissingFeatures.h"

// TODO(cir): once all builtins are covered, decide whether we still
// need to use LLVM intrinsics or if there's a better approach to follow. Right
// now the intrinsics are reused to make it convenient to encode all thousands
// of them and passing down to LLVM lowering.
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Value.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/AArch64TargetParser.h"

using namespace clang;
using namespace clang::CIRGen;
using namespace cir;
using namespace llvm;

template <typename... Operands>
static mlir::Value emitIntrinsicCallOpCached(CIRGenModule &cgm,
                                             CIRGenBuilderTy &builder,
                                             mlir::Location loc,
                                             const StringRef str,
                                             const mlir::Type &resTy,
                                             Operands &&...op) {
  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgm.getStringAttrCached(str), resTy,
             std::forward<Operands>(op)...)
      .getResult();
}

static void emitAArch64SetFPMR(CIRGenFunction &cgf, mlir::Location loc,
                               mlir::Value fpm) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  if (fpm.getType() != builder.getSInt64Ty())
    fpm = builder.createIntCast(fpm, builder.getSInt64Ty());
  cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgf.getCIRGenModule().getStringAttrCached("aarch64.set.fpmr"),
      mlir::Type{}, mlir::ValueRange{fpm});
}

static mlir::Value emitNeonCallCached(CIRGenFunction &cgf,
                                      llvm::SmallVector<mlir::Type> argTypes,
                                      llvm::SmallVectorImpl<mlir::Value> &args,
                                      llvm::StringRef intrinsicName,
                                      mlir::Type funcResTy,
                                      mlir::Location loc);

static mlir::Value emitAArch64FP8NeonCall(CIRGenFunction &cgf,
                                          mlir::Location loc,
                                          llvm::SmallVector<mlir::Type> argTypes,
                                          llvm::SmallVectorImpl<mlir::Value> &ops,
                                          llvm::StringRef intrinsicName,
                                          mlir::Type resultTy,
                                          const CallExpr *e) {
  emitAArch64SetFPMR(cgf, loc, cgf.emitScalarExpr(e->getArg(e->getNumArgs() - 1)));
  return emitNeonCallCached(cgf, std::move(argTypes), ops, intrinsicName,
                            resultTy, loc);
}

static mlir::Value emitAArch64VectorExtract(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            mlir::Type resultTy,
                                            mlir::Value vec) {
  return cir::LLVMIntrinsicCallOp::create(
             cgf.getBuilder(), loc, cgf.getCIRGenModule().getStringAttrCached("vector.extract"),
             resultTy, mlir::ValueRange{vec, cgf.getBuilder().getUInt64(0, loc)})
      .getResult();
}

static mlir::Value emitAArch64VectorInsert(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           mlir::Type resultTy,
                                           mlir::Value subVec) {
  mlir::Value poison =
      cgf.getBuilder().getConstant(loc, cir::PoisonAttr::get(resultTy));
  return cir::LLVMIntrinsicCallOp::create(
             cgf.getBuilder(), loc, cgf.getCIRGenModule().getStringAttrCached("vector.insert"),
             resultTy,
             mlir::ValueRange{poison, subVec, cgf.getBuilder().getUInt64(0, loc)})
      .getResult();
}

// Generate vscale * scalingFactor
static mlir::Value genVscaleTimesFactor(mlir::Location loc,
                                        CIRGenModule &cgm,
                                        CIRGenBuilderTy &builder,
                                        mlir::Type cirTy,
                                        int32_t scalingFactor) {
  mlir::Value vscale =
      emitIntrinsicCallOpCached(cgm, builder, loc, "vscale", cirTy);
  return builder.createNUWAMul(loc, vscale,
                               builder.getUInt64(scalingFactor, loc));
}

static bool aarch64SVEIntrinsicsProvenSorted = false;

// Struct for SVE builtins (different from ARMVectorIntrinsicInfo used for NEON)
namespace {
struct AArch64SVEBuiltinInfo {
  unsigned builtinID;
  unsigned llvmIntrinsic;
  uint64_t typeModifier;

  bool operator<(unsigned rhsBuiltinID) const {
    return builtinID < rhsBuiltinID;
  }
  bool operator<(const AArch64SVEBuiltinInfo &te) const {
    return builtinID < te.builtinID;
  }
};
} // end anonymous namespace

#define SVEMAP1(NameBase, llvmIntrinsic, TypeModifier)                         \
  {SVE::BI__builtin_sve_##NameBase, Intrinsic::llvmIntrinsic, TypeModifier}

#define SVEMAP2(NameBase, TypeModifier)                                        \
  {SVE::BI__builtin_sve_##NameBase, 0, TypeModifier}
static const AArch64SVEBuiltinInfo aarch64SVEIntrinsicMap[] = {
#define GET_SVE_LLVM_INTRINSIC_MAP
#include "clang/Basic/arm_sve_builtin_cg.inc"
#undef GET_SVE_LLVM_INTRINSIC_MAP
};
#undef SVEMAP1
#undef SVEMAP2

static const AArch64SVEBuiltinInfo *
findAArch64SVEBuiltinInMap(ArrayRef<AArch64SVEBuiltinInfo> intrinsicMap,
                           unsigned builtinID, bool &mapProvenSorted) {
#ifndef NDEBUG
  if (!mapProvenSorted) {
    assert(llvm::is_sorted(intrinsicMap));
    mapProvenSorted = true;
  }
#endif

  const AArch64SVEBuiltinInfo *info =
      llvm::lower_bound(intrinsicMap, builtinID);

  if (info != intrinsicMap.end() && info->builtinID == builtinID)
    return info;

  return nullptr;
}

enum {
  AddRetType = (1 << 0),
  Add1ArgType = (1 << 1),
  Add2ArgTypes = (1 << 2),

  VectorizeRetType = (1 << 3),
  VectorizeArgTypes = (1 << 4),

  InventFloatType = (1 << 5),
  UnsignedAlts = (1 << 6),

  Use64BitVectors = (1 << 7),
  Use128BitVectors = (1 << 8),

  Vectorize1ArgType = Add1ArgType | VectorizeArgTypes,
  VectorRet = AddRetType | VectorizeRetType,
  VectorRetGetArgs01 =
      AddRetType | Add2ArgTypes | VectorizeRetType | VectorizeArgTypes,
  FpCmpzModifiers =
      AddRetType | VectorizeRetType | Add1ArgType | InventFloatType
};

namespace {
struct ARMVectorIntrinsicInfo {
  const char *NameHint;
  unsigned BuiltinID;
  unsigned LLVMIntrinsic;
  unsigned AltLLVMIntrinsic;
  uint64_t TypeModifier;

  bool operator<(unsigned RHSBuiltinID) const {
    return BuiltinID < RHSBuiltinID;
  }
  bool operator<(const ARMVectorIntrinsicInfo &TE) const {
    return BuiltinID < TE.BuiltinID;
  }
};
} // end anonymous namespace

#define NEONMAP0(NameBase)                                                     \
  {#NameBase, NEON::BI__builtin_neon_##NameBase, 0, 0, 0}

#define NEONMAP1(NameBase, LLVMIntrinsic, TypeModifier)                        \
  {#NameBase, NEON::BI__builtin_neon_##NameBase, Intrinsic::LLVMIntrinsic, 0,  \
   TypeModifier}

#define NEONMAP2(NameBase, LLVMIntrinsic, AltLLVMIntrinsic, TypeModifier)      \
  {#NameBase, NEON::BI__builtin_neon_##NameBase, Intrinsic::LLVMIntrinsic,     \
   Intrinsic::AltLLVMIntrinsic, TypeModifier}

static const ARMVectorIntrinsicInfo AArch64SIMDIntrinsicMap[] = {
    NEONMAP0(splat_lane_v),
    NEONMAP0(splat_laneq_v),
    NEONMAP0(splatq_lane_v),
    NEONMAP0(splatq_laneq_v),
    NEONMAP1(vabs_v, aarch64_neon_abs, 0),
    NEONMAP1(vabsq_v, aarch64_neon_abs, 0),
    NEONMAP0(vadd_v),
    NEONMAP0(vaddhn_v),
    NEONMAP0(vaddq_p128),
    NEONMAP0(vaddq_v),
    NEONMAP1(vaesdq_u8, aarch64_crypto_aesd, 0),
    NEONMAP1(vaeseq_u8, aarch64_crypto_aese, 0),
    NEONMAP1(vaesimcq_u8, aarch64_crypto_aesimc, 0),
    NEONMAP1(vaesmcq_u8, aarch64_crypto_aesmc, 0),
    NEONMAP2(vbcaxq_s16, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_s32, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_s64, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_s8, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u16, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u32, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u64, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u8, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vbfdot_f32, aarch64_neon_bfdot, 0),
    NEONMAP1(vbfdotq_f32, aarch64_neon_bfdot, 0),
    NEONMAP1(vbfmlalbq_f32, aarch64_neon_bfmlalb, 0),
    NEONMAP1(vbfmlaltq_f32, aarch64_neon_bfmlalt, 0),
    NEONMAP1(vbfmmlaq_f32, aarch64_neon_bfmmla, 0),
    NEONMAP1(vcadd_rot270_f16, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcadd_rot270_f32, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcadd_rot90_f16, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcadd_rot90_f32, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcaddq_rot270_f16, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcaddq_rot270_f32, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcaddq_rot270_f64, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcaddq_rot90_f16, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcaddq_rot90_f32, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcaddq_rot90_f64, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcage_v, aarch64_neon_facge, 0),
    NEONMAP1(vcageq_v, aarch64_neon_facge, 0),
    NEONMAP1(vcagt_v, aarch64_neon_facgt, 0),
    NEONMAP1(vcagtq_v, aarch64_neon_facgt, 0),
    NEONMAP1(vcale_v, aarch64_neon_facge, 0),
    NEONMAP1(vcaleq_v, aarch64_neon_facge, 0),
    NEONMAP1(vcalt_v, aarch64_neon_facgt, 0),
    NEONMAP1(vcaltq_v, aarch64_neon_facgt, 0),
    NEONMAP0(vceqz_v),
    NEONMAP0(vceqzq_v),
    NEONMAP0(vcgez_v),
    NEONMAP0(vcgezq_v),
    NEONMAP0(vcgtz_v),
    NEONMAP0(vcgtzq_v),
    NEONMAP0(vclez_v),
    NEONMAP0(vclezq_v),
    NEONMAP1(vcls_v, aarch64_neon_cls, Add1ArgType),
    NEONMAP1(vclsq_v, aarch64_neon_cls, Add1ArgType),
    NEONMAP0(vcltz_v),
    NEONMAP0(vcltzq_v),
    NEONMAP1(vclz_v, ctlz, Add1ArgType),
    NEONMAP1(vclzq_v, ctlz, Add1ArgType),
    NEONMAP1(vcmla_f16, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmla_f32, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmla_rot180_f16, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmla_rot180_f32, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmla_rot270_f16, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmla_rot270_f32, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmla_rot90_f16, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmla_rot90_f32, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmlaq_f16, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmlaq_f32, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmlaq_f64, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmlaq_rot180_f16, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmlaq_rot180_f32, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmlaq_rot180_f64, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmlaq_rot270_f16, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmlaq_rot270_f32, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmlaq_rot270_f64, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmlaq_rot90_f16, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmlaq_rot90_f32, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmlaq_rot90_f64, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcnt_v, ctpop, Add1ArgType),
    NEONMAP1(vcntq_v, ctpop, Add1ArgType),
    NEONMAP1(vcvt_f16_f32, aarch64_neon_vcvtfp2hf, 0),
    NEONMAP0(vcvt_f16_s16),
    NEONMAP0(vcvt_f16_u16),
    NEONMAP1(vcvt_f32_f16, aarch64_neon_vcvthf2fp, 0),
    NEONMAP0(vcvt_f32_v),
    NEONMAP1(vcvt_n_f16_s16, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP1(vcvt_n_f16_u16, aarch64_neon_vcvtfxu2fp, 0),
    NEONMAP2(vcvt_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP2(vcvt_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP1(vcvt_n_s16_f16, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvt_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvt_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvt_n_u16_f16, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvt_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvt_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP0(vcvtq_f16_s16),
    NEONMAP0(vcvtq_f16_u16),
    NEONMAP0(vcvtq_f32_v),
    NEONMAP0(vcvtq_high_bf16_f32),
    NEONMAP0(vcvtq_low_bf16_f32),
    NEONMAP1(vcvtq_n_f16_s16, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP1(vcvtq_n_f16_u16, aarch64_neon_vcvtfxu2fp, 0),
    NEONMAP2(vcvtq_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp,
             0),
    NEONMAP2(vcvtq_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp,
             0),
    NEONMAP1(vcvtq_n_s16_f16, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvtq_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvtq_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvtq_n_u16_f16, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvtq_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvtq_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvtx_f32_v, aarch64_neon_fcvtxn, AddRetType | Add1ArgType),
    NEONMAP1(vdot_s32, aarch64_neon_sdot, 0),
    NEONMAP1(vdot_u32, aarch64_neon_udot, 0),
    NEONMAP1(vdotq_s32, aarch64_neon_sdot, 0),
    NEONMAP1(vdotq_u32, aarch64_neon_udot, 0),
    NEONMAP2(veor3q_s16, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_s32, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_s64, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_s8, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u16, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u32, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u64, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u8, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP0(vext_v),
    NEONMAP0(vextq_v),
    NEONMAP0(vfma_v),
    NEONMAP0(vfmaq_v),
    NEONMAP1(vfmlal_high_f16, aarch64_neon_fmlal2, 0),
    NEONMAP1(vfmlal_low_f16, aarch64_neon_fmlal, 0),
    NEONMAP1(vfmlalq_high_f16, aarch64_neon_fmlal2, 0),
    NEONMAP1(vfmlalq_low_f16, aarch64_neon_fmlal, 0),
    NEONMAP1(vfmlsl_high_f16, aarch64_neon_fmlsl2, 0),
    NEONMAP1(vfmlsl_low_f16, aarch64_neon_fmlsl, 0),
    NEONMAP1(vfmlslq_high_f16, aarch64_neon_fmlsl2, 0),
    NEONMAP1(vfmlslq_low_f16, aarch64_neon_fmlsl, 0),
    NEONMAP2(vhadd_v, aarch64_neon_uhadd, aarch64_neon_shadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vhaddq_v, aarch64_neon_uhadd, aarch64_neon_shadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vhsub_v, aarch64_neon_uhsub, aarch64_neon_shsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vhsubq_v, aarch64_neon_uhsub, aarch64_neon_shsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vld1_x2_v, aarch64_neon_ld1x2, 0),
    NEONMAP1(vld1_x3_v, aarch64_neon_ld1x3, 0),
    NEONMAP1(vld1_x4_v, aarch64_neon_ld1x4, 0),
    NEONMAP1(vld1q_x2_v, aarch64_neon_ld1x2, 0),
    NEONMAP1(vld1q_x3_v, aarch64_neon_ld1x3, 0),
    NEONMAP1(vld1q_x4_v, aarch64_neon_ld1x4, 0),
    NEONMAP1(vmmlaq_s32, aarch64_neon_smmla, 0),
    NEONMAP1(vmmlaq_u32, aarch64_neon_ummla, 0),
    NEONMAP0(vmovl_v),
    NEONMAP0(vmovn_v),
    NEONMAP1(vmul_v, aarch64_neon_pmul, Add1ArgType),
    NEONMAP1(vmulq_v, aarch64_neon_pmul, Add1ArgType),
    NEONMAP1(vpadd_v, aarch64_neon_addp, Add1ArgType),
    NEONMAP2(vpaddl_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
    NEONMAP2(vpaddlq_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
    NEONMAP1(vpaddq_v, aarch64_neon_addp, Add1ArgType),
    NEONMAP1(vqabs_v, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP1(vqabsq_v, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP2(vqadd_v, aarch64_neon_uqadd, aarch64_neon_sqadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqaddq_v, aarch64_neon_uqadd, aarch64_neon_sqadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqdmlal_v, aarch64_neon_sqdmull, aarch64_neon_sqadd, 0),
    NEONMAP2(vqdmlsl_v, aarch64_neon_sqdmull, aarch64_neon_sqsub, 0),
    NEONMAP1(vqdmulh_lane_v, aarch64_neon_sqdmulh_lane, 0),
    NEONMAP1(vqdmulh_laneq_v, aarch64_neon_sqdmulh_laneq, 0),
    NEONMAP1(vqdmulh_v, aarch64_neon_sqdmulh, Add1ArgType),
    NEONMAP1(vqdmulhq_lane_v, aarch64_neon_sqdmulh_lane, 0),
    NEONMAP1(vqdmulhq_laneq_v, aarch64_neon_sqdmulh_laneq, 0),
    NEONMAP1(vqdmulhq_v, aarch64_neon_sqdmulh, Add1ArgType),
    NEONMAP1(vqdmull_v, aarch64_neon_sqdmull, Add1ArgType),
    NEONMAP2(vqmovn_v, aarch64_neon_uqxtn, aarch64_neon_sqxtn,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vqmovun_v, aarch64_neon_sqxtun, Add1ArgType),
    NEONMAP1(vqneg_v, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqnegq_v, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqrdmlah_s16, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlah_s32, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlahq_s16, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlahq_s32, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlsh_s16, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmlsh_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmlshq_s16, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmlshq_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmulh_lane_v, aarch64_neon_sqrdmulh_lane, 0),
    NEONMAP1(vqrdmulh_laneq_v, aarch64_neon_sqrdmulh_laneq, 0),
    NEONMAP1(vqrdmulh_v, aarch64_neon_sqrdmulh, Add1ArgType),
    NEONMAP1(vqrdmulhq_lane_v, aarch64_neon_sqrdmulh_lane, 0),
    NEONMAP1(vqrdmulhq_laneq_v, aarch64_neon_sqrdmulh_laneq, 0),
    NEONMAP1(vqrdmulhq_v, aarch64_neon_sqrdmulh, Add1ArgType),
    NEONMAP2(vqrshl_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqrshlq_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqshl_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl, UnsignedAlts),
    NEONMAP2(vqshl_v, aarch64_neon_uqshl, aarch64_neon_sqshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqshlq_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl, UnsignedAlts),
    NEONMAP2(vqshlq_v, aarch64_neon_uqshl, aarch64_neon_sqshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vqshlu_n_v, aarch64_neon_sqshlu, 0),
    NEONMAP1(vqshluq_n_v, aarch64_neon_sqshlu, 0),
    NEONMAP2(vqsub_v, aarch64_neon_uqsub, aarch64_neon_sqsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqsubq_v, aarch64_neon_uqsub, aarch64_neon_sqsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vraddhn_v, aarch64_neon_raddhn, Add1ArgType),
    NEONMAP1(vrax1q_u64, aarch64_crypto_rax1, 0),
    NEONMAP2(vrecpe_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
    NEONMAP2(vrecpeq_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
    NEONMAP1(vrecps_v, aarch64_neon_frecps, Add1ArgType),
    NEONMAP1(vrecpsq_v, aarch64_neon_frecps, Add1ArgType),
    NEONMAP2(vrhadd_v, aarch64_neon_urhadd, aarch64_neon_srhadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vrhaddq_v, aarch64_neon_urhadd, aarch64_neon_srhadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vrnd32x_f32, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32x_f64, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32xq_f32, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32xq_f64, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32z_f32, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd32z_f64, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd32zq_f32, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd32zq_f64, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd64x_f32, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64x_f64, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64xq_f32, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64xq_f64, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64z_f32, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP1(vrnd64z_f64, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP1(vrnd64zq_f32, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP1(vrnd64zq_f64, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP0(vrndi_v),
    NEONMAP0(vrndiq_v),
    NEONMAP2(vrshl_v, aarch64_neon_urshl, aarch64_neon_srshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vrshlq_v, aarch64_neon_urshl, aarch64_neon_srshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vrshr_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
    NEONMAP2(vrshrq_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
    NEONMAP2(vrsqrte_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
    NEONMAP2(vrsqrteq_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
    NEONMAP1(vrsqrts_v, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vrsqrtsq_v, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vrsubhn_v, aarch64_neon_rsubhn, Add1ArgType),
    NEONMAP1(vsha1su0q_u32, aarch64_crypto_sha1su0, 0),
    NEONMAP1(vsha1su1q_u32, aarch64_crypto_sha1su1, 0),
    NEONMAP1(vsha256h2q_u32, aarch64_crypto_sha256h2, 0),
    NEONMAP1(vsha256hq_u32, aarch64_crypto_sha256h, 0),
    NEONMAP1(vsha256su0q_u32, aarch64_crypto_sha256su0, 0),
    NEONMAP1(vsha256su1q_u32, aarch64_crypto_sha256su1, 0),
    NEONMAP1(vsha512h2q_u64, aarch64_crypto_sha512h2, 0),
    NEONMAP1(vsha512hq_u64, aarch64_crypto_sha512h, 0),
    NEONMAP1(vsha512su0q_u64, aarch64_crypto_sha512su0, 0),
    NEONMAP1(vsha512su1q_u64, aarch64_crypto_sha512su1, 0),
    NEONMAP0(vshl_n_v),
    NEONMAP2(vshl_v, aarch64_neon_ushl, aarch64_neon_sshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP0(vshll_n_v),
    NEONMAP0(vshlq_n_v),
    NEONMAP2(vshlq_v, aarch64_neon_ushl, aarch64_neon_sshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP0(vshr_n_v),
    NEONMAP0(vshrn_n_v),
    NEONMAP0(vshrq_n_v),
    NEONMAP1(vsm3partw1q_u32, aarch64_crypto_sm3partw1, 0),
    NEONMAP1(vsm3partw2q_u32, aarch64_crypto_sm3partw2, 0),
    NEONMAP1(vsm3ss1q_u32, aarch64_crypto_sm3ss1, 0),
    NEONMAP1(vsm3tt1aq_u32, aarch64_crypto_sm3tt1a, 0),
    NEONMAP1(vsm3tt1bq_u32, aarch64_crypto_sm3tt1b, 0),
    NEONMAP1(vsm3tt2aq_u32, aarch64_crypto_sm3tt2a, 0),
    NEONMAP1(vsm3tt2bq_u32, aarch64_crypto_sm3tt2b, 0),
    NEONMAP1(vsm4ekeyq_u32, aarch64_crypto_sm4ekey, 0),
    NEONMAP1(vsm4eq_u32, aarch64_crypto_sm4e, 0),
    NEONMAP1(vst1_x2_v, aarch64_neon_st1x2, 0),
    NEONMAP1(vst1_x3_v, aarch64_neon_st1x3, 0),
    NEONMAP1(vst1_x4_v, aarch64_neon_st1x4, 0),
    NEONMAP1(vst1q_x2_v, aarch64_neon_st1x2, 0),
    NEONMAP1(vst1q_x3_v, aarch64_neon_st1x3, 0),
    NEONMAP1(vst1q_x4_v, aarch64_neon_st1x4, 0),
    NEONMAP0(vsubhn_v),
    NEONMAP0(vtst_v),
    NEONMAP0(vtstq_v),
    NEONMAP1(vusdot_s32, aarch64_neon_usdot, 0),
    NEONMAP1(vusdotq_s32, aarch64_neon_usdot, 0),
    NEONMAP1(vusmmlaq_s32, aarch64_neon_usmmla, 0),
    NEONMAP1(vxarq_u64, aarch64_crypto_xar, 0),
};

static const ARMVectorIntrinsicInfo AArch64SISDIntrinsicMap[] = {
    NEONMAP1(vabdd_f64, aarch64_sisd_fabd, Add1ArgType),
    NEONMAP1(vabds_f32, aarch64_sisd_fabd, Add1ArgType),
    NEONMAP1(vabsd_s64, aarch64_neon_abs, Add1ArgType),
    NEONMAP1(vaddlv_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddlv_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddlvq_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddlvq_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddv_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddv_s32, vector_reduce_add, Add1ArgType),
    NEONMAP1(vaddv_u32, vector_reduce_add, Add1ArgType),
    NEONMAP1(vaddvq_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_f64, aarch64_neon_faddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_s32, vector_reduce_add, Add1ArgType),
    NEONMAP1(vaddvq_s64, vector_reduce_add, Add1ArgType),
    NEONMAP1(vaddvq_u32, vector_reduce_add, Add1ArgType),
    NEONMAP1(vaddvq_u64, vector_reduce_add, Add1ArgType),
    NEONMAP1(vcaged_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcages_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcagtd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcagts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcaled_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcales_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcaltd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcalts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcvtad_s32_f64, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtad_s64_f64, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtad_u32_f64, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtad_u64_f64, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtas_s32_f32, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtas_s64_f32, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtas_u32_f32, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtas_u64_f32, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_f64_s64, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_f64_u64, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_s64_f64, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_u64_f64, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_s32_f64, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_s64_f64, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_u32_f64, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_u64_f64, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP0(vcvth_bf16_f32),
    NEONMAP1(vcvtmd_s32_f64, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmd_s64_f64, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmd_u32_f64, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmd_u64_f64, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtms_s32_f32, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtms_s64_f32, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtms_u32_f32, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtms_u64_f32, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnd_s32_f64, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnd_s64_f64, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnd_u32_f64, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnd_u64_f64, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtns_s32_f32, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtns_s64_f32, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtns_u32_f32, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtns_u64_f32, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtpd_s32_f64, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtpd_s64_f64, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtpd_u32_f64, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtpd_u64_f64, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtps_s32_f32, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtps_s64_f32, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtps_u32_f32, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtps_u64_f32, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_f32_s32, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_f32_u32, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_s32_f32, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_u32_f32, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_s32_f32, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvts_s64_f32, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvts_u32_f32, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvts_u64_f32, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtxd_f32_f64, aarch64_sisd_fcvtxn, 0),
    NEONMAP1(vmaxnmv_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxnmvq_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxnmvq_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxv_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxv_s32, vector_reduce_smax, Add1ArgType),
    NEONMAP1(vmaxv_u32, vector_reduce_umax, Add1ArgType),
    NEONMAP1(vmaxvq_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxvq_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxvq_s32, vector_reduce_smax, Add1ArgType),
    NEONMAP1(vmaxvq_u32, vector_reduce_umax, Add1ArgType),
    NEONMAP1(vminnmv_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vminnmvq_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vminnmvq_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vminv_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vminv_s32, vector_reduce_smin, Add1ArgType),
    NEONMAP1(vminv_u32, vector_reduce_umin, Add1ArgType),
    NEONMAP1(vminvq_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vminvq_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vminvq_s32, vector_reduce_smin, Add1ArgType),
    NEONMAP1(vminvq_u32, vector_reduce_umin, Add1ArgType),
    NEONMAP1(vmull_p64, aarch64_neon_pmull64, 0),
    NEONMAP1(vmulxd_f64, aarch64_neon_fmulx, Add1ArgType),
    NEONMAP1(vmulxs_f32, aarch64_neon_fmulx, Add1ArgType),
    NEONMAP1(vpaddd_s64, vector_reduce_add, Add1ArgType),
    NEONMAP1(vpaddd_u64, vector_reduce_add, Add1ArgType),
    NEONMAP1(vpmaxnmqd_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxnms_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxqd_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxs_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vpminnmqd_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpminnms_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpminqd_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vpmins_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vqabsb_s8, aarch64_neon_sqabs,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqabsd_s64, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP1(vqabsh_s16, aarch64_neon_sqabs,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqabss_s32, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP1(vqaddb_s8, aarch64_neon_sqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqaddb_u8, aarch64_neon_uqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqaddd_s64, aarch64_neon_sqadd, Add1ArgType),
    NEONMAP1(vqaddd_u64, aarch64_neon_uqadd, Add1ArgType),
    NEONMAP1(vqaddh_s16, aarch64_neon_sqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqaddh_u16, aarch64_neon_uqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqadds_s32, aarch64_neon_sqadd, Add1ArgType),
    NEONMAP1(vqadds_u32, aarch64_neon_uqadd, Add1ArgType),
    NEONMAP1(vqdmulhh_s16, aarch64_neon_sqdmulh,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqdmulhs_s32, aarch64_neon_sqdmulh, Add1ArgType),
    NEONMAP1(vqdmullh_s16, aarch64_neon_sqdmull, VectorRet | Use128BitVectors),
    NEONMAP1(vqdmulls_s32, aarch64_neon_sqdmulls_scalar, 0),
    NEONMAP1(vqmovnd_s64, aarch64_neon_scalar_sqxtn, AddRetType | Add1ArgType),
    NEONMAP1(vqmovnd_u64, aarch64_neon_scalar_uqxtn, AddRetType | Add1ArgType),
    NEONMAP1(vqmovnh_s16, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovnh_u16, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovns_s32, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovns_u32, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovund_s64, aarch64_neon_scalar_sqxtun,
             AddRetType | Add1ArgType),
    NEONMAP1(vqmovunh_s16, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovuns_s32, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
    NEONMAP1(vqnegb_s8, aarch64_neon_sqneg,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqnegd_s64, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqnegh_s16, aarch64_neon_sqneg,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqnegs_s32, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqrdmlahh_s16, aarch64_neon_sqrdmlah,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrdmlahs_s32, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlshh_s16, aarch64_neon_sqrdmlsh,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrdmlshs_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmulhh_s16, aarch64_neon_sqrdmulh,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrdmulhs_s32, aarch64_neon_sqrdmulh, Add1ArgType),
    NEONMAP1(vqrshlb_s8, aarch64_neon_sqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshlb_u8, aarch64_neon_uqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshld_s64, aarch64_neon_sqrshl, Add1ArgType),
    NEONMAP1(vqrshld_u64, aarch64_neon_uqrshl, Add1ArgType),
    NEONMAP1(vqrshlh_s16, aarch64_neon_sqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshlh_u16, aarch64_neon_uqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshls_s32, aarch64_neon_sqrshl, Add1ArgType),
    NEONMAP1(vqrshls_u32, aarch64_neon_uqrshl, Add1ArgType),
    NEONMAP1(vqrshrnd_n_s64, aarch64_neon_sqrshrn, AddRetType),
    NEONMAP1(vqrshrnd_n_u64, aarch64_neon_uqrshrn, AddRetType),
    NEONMAP1(vqrshrnh_n_s16, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrnh_n_u16, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrns_n_s32, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrns_n_u32, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrund_n_s64, aarch64_neon_sqrshrun, AddRetType),
    NEONMAP1(vqrshrunh_n_s16, aarch64_neon_sqrshrun,
             VectorRet | Use64BitVectors),
    NEONMAP1(vqrshruns_n_s32, aarch64_neon_sqrshrun,
             VectorRet | Use64BitVectors),
    NEONMAP1(vqshlb_n_s8, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlb_n_u8, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlb_s8, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlb_u8, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshld_s64, aarch64_neon_sqshl, Add1ArgType),
    NEONMAP1(vqshld_u64, aarch64_neon_uqshl, Add1ArgType),
    NEONMAP1(vqshlh_n_s16, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlh_n_u16, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlh_s16, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlh_u16, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshls_n_s32, aarch64_neon_sqshl, Add1ArgType),
    NEONMAP1(vqshls_n_u32, aarch64_neon_uqshl, Add1ArgType),
    NEONMAP1(vqshls_s32, aarch64_neon_sqshl, Add1ArgType),
    NEONMAP1(vqshls_u32, aarch64_neon_uqshl, Add1ArgType),
    NEONMAP1(vqshlub_n_s8, aarch64_neon_sqshlu,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshluh_n_s16, aarch64_neon_sqshlu,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlus_n_s32, aarch64_neon_sqshlu, Add1ArgType),
    NEONMAP1(vqshrnd_n_s64, aarch64_neon_sqshrn, AddRetType),
    NEONMAP1(vqshrnd_n_u64, aarch64_neon_uqshrn, AddRetType),
    NEONMAP1(vqshrnh_n_s16, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrnh_n_u16, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrns_n_s32, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrns_n_u32, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrund_n_s64, aarch64_neon_sqshrun, AddRetType),
    NEONMAP1(vqshrunh_n_s16, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
    NEONMAP1(vqshruns_n_s32, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
    NEONMAP1(vqsubb_s8, aarch64_neon_sqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubb_u8, aarch64_neon_uqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubd_s64, aarch64_neon_sqsub, Add1ArgType),
    NEONMAP1(vqsubd_u64, aarch64_neon_uqsub, Add1ArgType),
    NEONMAP1(vqsubh_s16, aarch64_neon_sqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubh_u16, aarch64_neon_uqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubs_s32, aarch64_neon_sqsub, Add1ArgType),
    NEONMAP1(vqsubs_u32, aarch64_neon_uqsub, Add1ArgType),
    NEONMAP1(vrecped_f64, aarch64_neon_frecpe, Add1ArgType),
    NEONMAP1(vrecpes_f32, aarch64_neon_frecpe, Add1ArgType),
    NEONMAP1(vrecpxd_f64, aarch64_neon_frecpx, Add1ArgType),
    NEONMAP1(vrecpxs_f32, aarch64_neon_frecpx, Add1ArgType),
    NEONMAP1(vrshld_s64, aarch64_neon_srshl, Add1ArgType),
    NEONMAP1(vrshld_u64, aarch64_neon_urshl, Add1ArgType),
    NEONMAP1(vrsqrted_f64, aarch64_neon_frsqrte, Add1ArgType),
    NEONMAP1(vrsqrtes_f32, aarch64_neon_frsqrte, Add1ArgType),
    NEONMAP1(vrsqrtsd_f64, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vrsqrtss_f32, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vsha1cq_u32, aarch64_crypto_sha1c, 0),
    NEONMAP1(vsha1h_u32, aarch64_crypto_sha1h, 0),
    NEONMAP1(vsha1mq_u32, aarch64_crypto_sha1m, 0),
    NEONMAP1(vsha1pq_u32, aarch64_crypto_sha1p, 0),
    NEONMAP1(vshld_s64, aarch64_neon_sshl, Add1ArgType),
    NEONMAP1(vshld_u64, aarch64_neon_ushl, Add1ArgType),
    NEONMAP1(vslid_n_s64, aarch64_neon_vsli, Vectorize1ArgType),
    NEONMAP1(vslid_n_u64, aarch64_neon_vsli, Vectorize1ArgType),
    NEONMAP1(vsqaddb_u8, aarch64_neon_usqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vsqaddd_u64, aarch64_neon_usqadd, Add1ArgType),
    NEONMAP1(vsqaddh_u16, aarch64_neon_usqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vsqadds_u32, aarch64_neon_usqadd, Add1ArgType),
    NEONMAP1(vsrid_n_s64, aarch64_neon_vsri, Vectorize1ArgType),
    NEONMAP1(vsrid_n_u64, aarch64_neon_vsri, Vectorize1ArgType),
    NEONMAP1(vuqaddb_s8, aarch64_neon_suqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vuqaddd_s64, aarch64_neon_suqadd, Add1ArgType),
    NEONMAP1(vuqaddh_s16, aarch64_neon_suqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vuqadds_s32, aarch64_neon_suqadd, Add1ArgType),
    // FP16 scalar intrinisics go here.
    NEONMAP1(vabdh_f16, aarch64_sisd_fabd, Add1ArgType),
    NEONMAP1(vcvtah_s32_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtah_s64_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtah_u32_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtah_u64_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_s32, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_s64, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_u32, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_u64, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_s32_f16, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_s64_f16, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_u32_f16, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_u64_f16, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_s32_f16, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_s64_f16, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_u32_f16, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_u64_f16, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_s32_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_s64_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_u32_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_u64_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_s32_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_s64_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_u32_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_u64_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_s32_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_s64_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_u32_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_u64_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vmulxh_f16, aarch64_neon_fmulx, Add1ArgType),
    NEONMAP1(vrecpeh_f16, aarch64_neon_frecpe, Add1ArgType),
    NEONMAP1(vrecpxh_f16, aarch64_neon_frecpx, Add1ArgType),
    NEONMAP1(vrsqrteh_f16, aarch64_neon_frsqrte, Add1ArgType),
    NEONMAP1(vrsqrtsh_f16, aarch64_neon_frsqrts, Add1ArgType),
};

/// Get name of intrinsics in the AArch64SIMDIntrinsicMap defined above.
static std::string getAArch64SIMDIntrinsicString(unsigned int intrinsicID) {
  switch (intrinsicID) {
  default:
    return std::string("Unexpected intrinsic id " +
                       std::to_string(intrinsicID));
  case NEON::BI__builtin_neon_splat_lane_v:
    return "NEON::BI__builtin_neon_splat_lane_v";
  case NEON::BI__builtin_neon_splat_laneq_v:
    return "NEON::BI__builtin_neon_splat_laneq_v";
  case NEON::BI__builtin_neon_splatq_lane_v:
    return "NEON::BI__builtin_neon_splatq_lane_v";
  case NEON::BI__builtin_neon_splatq_laneq_v:
    return "NEON::BI__builtin_neon_splatq_laneq_v";
  case NEON::BI__builtin_neon_vabs_v:
    return "NEON::BI__builtin_neon_vabs_v";
  case NEON::BI__builtin_neon_vabsq_v:
    return "NEON::BI__builtin_neon_vabsq_v";
  case NEON::BI__builtin_neon_vadd_v:
    return "NEON::BI__builtin_neon_vadd_v";
  case NEON::BI__builtin_neon_vaddhn_v:
    return "NEON::BI__builtin_neon_vaddhn_v";
  case NEON::BI__builtin_neon_vaddq_p128:
    return "NEON::BI__builtin_neon_vaddq_p128";
  case NEON::BI__builtin_neon_vaddq_v:
    return "NEON::BI__builtin_neon_vaddq_v";
  case NEON::BI__builtin_neon_vaesdq_u8:
    return "NEON::BI__builtin_neon_vaesdq_u8";
  case NEON::BI__builtin_neon_vaeseq_u8:
    return "NEON::BI__builtin_neon_vaeseq_u8";
  case NEON::BI__builtin_neon_vaesimcq_u8:
    return "NEON::BI__builtin_neon_vaesimcq_u8";
  case NEON::BI__builtin_neon_vaesmcq_u8:
    return "NEON::BI__builtin_neon_vaesmcq_u8";
  case NEON::BI__builtin_neon_vbcaxq_s16:
    return "NEON::BI__builtin_neon_vbcaxq_s16";
  case NEON::BI__builtin_neon_vbcaxq_s32:
    return "NEON::BI__builtin_neon_vbcaxq_s32";
  case NEON::BI__builtin_neon_vbcaxq_s64:
    return "NEON::BI__builtin_neon_vbcaxq_s64";
  case NEON::BI__builtin_neon_vbcaxq_s8:
    return "NEON::BI__builtin_neon_vbcaxq_s8";
  case NEON::BI__builtin_neon_vbcaxq_u16:
    return "NEON::BI__builtin_neon_vbcaxq_u16";
  case NEON::BI__builtin_neon_vbcaxq_u32:
    return "NEON::BI__builtin_neon_vbcaxq_u32";
  case NEON::BI__builtin_neon_vbcaxq_u64:
    return "NEON::BI__builtin_neon_vbcaxq_u64";
  case NEON::BI__builtin_neon_vbcaxq_u8:
    return "NEON::BI__builtin_neon_vbcaxq_u8";
  case NEON::BI__builtin_neon_vbfdot_f32:
    return "NEON::BI__builtin_neon_vbfdot_f32";
  case NEON::BI__builtin_neon_vbfdotq_f32:
    return "NEON::BI__builtin_neon_vbfdotq_f32";
  case NEON::BI__builtin_neon_vbfmlalbq_f32:
    return "NEON::BI__builtin_neon_vbfmlalbq_f32";
  case NEON::BI__builtin_neon_vbfmlaltq_f32:
    return "NEON::BI__builtin_neon_vbfmlaltq_f32";
  case NEON::BI__builtin_neon_vbfmmlaq_f32:
    return "NEON::BI__builtin_neon_vbfmmlaq_f32";
  case NEON::BI__builtin_neon_vcadd_rot270_f16:
    return "NEON::BI__builtin_neon_vcadd_rot270_f16";
  case NEON::BI__builtin_neon_vcadd_rot270_f32:
    return "NEON::BI__builtin_neon_vcadd_rot270_f32";
  case NEON::BI__builtin_neon_vcadd_rot90_f16:
    return "NEON::BI__builtin_neon_vcadd_rot90_f16";
  case NEON::BI__builtin_neon_vcadd_rot90_f32:
    return "NEON::BI__builtin_neon_vcadd_rot90_f32";
  case NEON::BI__builtin_neon_vcaddq_rot270_f16:
    return "NEON::BI__builtin_neon_vcaddq_rot270_f16";
  case NEON::BI__builtin_neon_vcaddq_rot270_f32:
    return "NEON::BI__builtin_neon_vcaddq_rot270_f32";
  case NEON::BI__builtin_neon_vcaddq_rot270_f64:
    return "NEON::BI__builtin_neon_vcaddq_rot270_f64";
  case NEON::BI__builtin_neon_vcaddq_rot90_f16:
    return "NEON::BI__builtin_neon_vcaddq_rot90_f16";
  case NEON::BI__builtin_neon_vcaddq_rot90_f32:
    return "NEON::BI__builtin_neon_vcaddq_rot90_f32";
  case NEON::BI__builtin_neon_vcaddq_rot90_f64:
    return "NEON::BI__builtin_neon_vcaddq_rot90_f64";
  case NEON::BI__builtin_neon_vcage_v:
    return "NEON::BI__builtin_neon_vcage_v";
  case NEON::BI__builtin_neon_vcageq_v:
    return "NEON::BI__builtin_neon_vcageq_v";
  case NEON::BI__builtin_neon_vcagt_v:
    return "NEON::BI__builtin_neon_vcagt_v";
  case NEON::BI__builtin_neon_vcagtq_v:
    return "NEON::BI__builtin_neon_vcagtq_v";
  case NEON::BI__builtin_neon_vcale_v:
    return "NEON::BI__builtin_neon_vcale_v";
  case NEON::BI__builtin_neon_vcaleq_v:
    return "NEON::BI__builtin_neon_vcaleq_v";
  case NEON::BI__builtin_neon_vcalt_v:
    return "NEON::BI__builtin_neon_vcalt_v";
  case NEON::BI__builtin_neon_vcaltq_v:
    return "NEON::BI__builtin_neon_vcaltq_v";
  case NEON::BI__builtin_neon_vceqz_v:
    return "NEON::BI__builtin_neon_vceqz_v";
  case NEON::BI__builtin_neon_vceqzq_v:
    return "NEON::BI__builtin_neon_vceqzq_v";
  case NEON::BI__builtin_neon_vcgez_v:
    return "NEON::BI__builtin_neon_vcgez_v";
  case NEON::BI__builtin_neon_vcgezq_v:
    return "NEON::BI__builtin_neon_vcgezq_v";
  case NEON::BI__builtin_neon_vcgtz_v:
    return "NEON::BI__builtin_neon_vcgtz_v";
  case NEON::BI__builtin_neon_vcgtzq_v:
    return "NEON::BI__builtin_neon_vcgtzq_v";
  case NEON::BI__builtin_neon_vclez_v:
    return "NEON::BI__builtin_neon_vclez_v";
  case NEON::BI__builtin_neon_vclezq_v:
    return "NEON::BI__builtin_neon_vclezq_v";
  case NEON::BI__builtin_neon_vcls_v:
    return "NEON::BI__builtin_neon_vcls_v";
  case NEON::BI__builtin_neon_vclsq_v:
    return "NEON::BI__builtin_neon_vclsq_v";
  case NEON::BI__builtin_neon_vcltz_v:
    return "NEON::BI__builtin_neon_vcltz_v";
  case NEON::BI__builtin_neon_vcltzq_v:
    return "NEON::BI__builtin_neon_vcltzq_v";
  case NEON::BI__builtin_neon_vclz_v:
    return "NEON::BI__builtin_neon_vclz_v";
  case NEON::BI__builtin_neon_vclzq_v:
    return "NEON::BI__builtin_neon_vclzq_v";
  case NEON::BI__builtin_neon_vcmla_f16:
    return "NEON::BI__builtin_neon_vcmla_f16";
  case NEON::BI__builtin_neon_vcmla_f32:
    return "NEON::BI__builtin_neon_vcmla_f32";
  case NEON::BI__builtin_neon_vcmla_rot180_f16:
    return "NEON::BI__builtin_neon_vcmla_rot180_f16";
  case NEON::BI__builtin_neon_vcmla_rot180_f32:
    return "NEON::BI__builtin_neon_vcmla_rot180_f32";
  case NEON::BI__builtin_neon_vcmla_rot270_f16:
    return "NEON::BI__builtin_neon_vcmla_rot270_f16";
  case NEON::BI__builtin_neon_vcmla_rot270_f32:
    return "NEON::BI__builtin_neon_vcmla_rot270_f32";
  case NEON::BI__builtin_neon_vcmla_rot90_f16:
    return "NEON::BI__builtin_neon_vcmla_rot90_f16";
  case NEON::BI__builtin_neon_vcmla_rot90_f32:
    return "NEON::BI__builtin_neon_vcmla_rot90_f32";
  case NEON::BI__builtin_neon_vcmlaq_f16:
    return "NEON::BI__builtin_neon_vcmlaq_f16";
  case NEON::BI__builtin_neon_vcmlaq_f32:
    return "NEON::BI__builtin_neon_vcmlaq_f32";
  case NEON::BI__builtin_neon_vcmlaq_f64:
    return "NEON::BI__builtin_neon_vcmlaq_f64";
  case NEON::BI__builtin_neon_vcmlaq_rot180_f16:
    return "NEON::BI__builtin_neon_vcmlaq_rot180_f16";
  case NEON::BI__builtin_neon_vcmlaq_rot180_f32:
    return "NEON::BI__builtin_neon_vcmlaq_rot180_f32";
  case NEON::BI__builtin_neon_vcmlaq_rot180_f64:
    return "NEON::BI__builtin_neon_vcmlaq_rot180_f64";
  case NEON::BI__builtin_neon_vcmlaq_rot270_f16:
    return "NEON::BI__builtin_neon_vcmlaq_rot270_f16";
  case NEON::BI__builtin_neon_vcmlaq_rot270_f32:
    return "NEON::BI__builtin_neon_vcmlaq_rot270_f32";
  case NEON::BI__builtin_neon_vcmlaq_rot270_f64:
    return "NEON::BI__builtin_neon_vcmlaq_rot270_f64";
  case NEON::BI__builtin_neon_vcmlaq_rot90_f16:
    return "NEON::BI__builtin_neon_vcmlaq_rot90_f16";
  case NEON::BI__builtin_neon_vcmlaq_rot90_f32:
    return "NEON::BI__builtin_neon_vcmlaq_rot90_f32";
  case NEON::BI__builtin_neon_vcmlaq_rot90_f64:
    return "NEON::BI__builtin_neon_vcmlaq_rot90_f64";
  case NEON::BI__builtin_neon_vcnt_v:
    return "NEON::BI__builtin_neon_vcnt_v";
  case NEON::BI__builtin_neon_vcntq_v:
    return "NEON::BI__builtin_neon_vcntq_v";
  case NEON::BI__builtin_neon_vcvt_f16_f32:
    return "NEON::BI__builtin_neon_vcvt_f16_f32";
  case NEON::BI__builtin_neon_vcvt_f16_s16:
    return "NEON::BI__builtin_neon_vcvt_f16_s16";
  case NEON::BI__builtin_neon_vcvt_f16_u16:
    return "NEON::BI__builtin_neon_vcvt_f16_u16";
  case NEON::BI__builtin_neon_vcvt_f32_f16:
    return "NEON::BI__builtin_neon_vcvt_f32_f16";
  case NEON::BI__builtin_neon_vcvt_f32_v:
    return "NEON::BI__builtin_neon_vcvt_f32_v";
  case NEON::BI__builtin_neon_vcvt_n_f16_s16:
    return "NEON::BI__builtin_neon_vcvt_n_f16_s16";
  case NEON::BI__builtin_neon_vcvt_n_f16_u16:
    return "NEON::BI__builtin_neon_vcvt_n_f16_u16";
  case NEON::BI__builtin_neon_vcvt_n_f32_v:
    return "NEON::BI__builtin_neon_vcvt_n_f32_v";
  case NEON::BI__builtin_neon_vcvt_n_f64_v:
    return "NEON::BI__builtin_neon_vcvt_n_f64_v";
  case NEON::BI__builtin_neon_vcvt_n_s16_f16:
    return "NEON::BI__builtin_neon_vcvt_n_s16_f16";
  case NEON::BI__builtin_neon_vcvt_n_s32_v:
    return "NEON::BI__builtin_neon_vcvt_n_s32_v";
  case NEON::BI__builtin_neon_vcvt_n_s64_v:
    return "NEON::BI__builtin_neon_vcvt_n_s64_v";
  case NEON::BI__builtin_neon_vcvt_n_u16_f16:
    return "NEON::BI__builtin_neon_vcvt_n_u16_f16";
  case NEON::BI__builtin_neon_vcvt_n_u32_v:
    return "NEON::BI__builtin_neon_vcvt_n_u32_v";
  case NEON::BI__builtin_neon_vcvt_n_u64_v:
    return "NEON::BI__builtin_neon_vcvt_n_u64_v";
  case NEON::BI__builtin_neon_vcvtq_f16_s16:
    return "NEON::BI__builtin_neon_vcvtq_f16_s16";
  case NEON::BI__builtin_neon_vcvtq_f16_u16:
    return "NEON::BI__builtin_neon_vcvtq_f16_u16";
  case NEON::BI__builtin_neon_vcvtq_f32_v:
    return "NEON::BI__builtin_neon_vcvtq_f32_v";
  case NEON::BI__builtin_neon_vcvtq_high_bf16_f32:
    return "NEON::BI__builtin_neon_vcvtq_high_bf16_f32";
  case NEON::BI__builtin_neon_vcvtq_n_f16_s16:
    return "NEON::BI__builtin_neon_vcvtq_n_f16_s16";
  case NEON::BI__builtin_neon_vcvtq_n_f16_u16:
    return "NEON::BI__builtin_neon_vcvtq_n_f16_u16";
  case NEON::BI__builtin_neon_vcvtq_n_f32_v:
    return "NEON::BI__builtin_neon_vcvtq_n_f32_v";
  case NEON::BI__builtin_neon_vcvtq_n_f64_v:
    return "NEON::BI__builtin_neon_vcvtq_n_f64_v";
  case NEON::BI__builtin_neon_vcvtq_n_s16_f16:
    return "NEON::BI__builtin_neon_vcvtq_n_s16_f16";
  case NEON::BI__builtin_neon_vcvtq_n_s32_v:
    return "NEON::BI__builtin_neon_vcvtq_n_s32_v";
  case NEON::BI__builtin_neon_vcvtq_n_s64_v:
    return "NEON::BI__builtin_neon_vcvtq_n_s64_v";
  case NEON::BI__builtin_neon_vcvtq_n_u16_f16:
    return "NEON::BI__builtin_neon_vcvtq_n_u16_f16";
  case NEON::BI__builtin_neon_vcvtq_n_u32_v:
    return "NEON::BI__builtin_neon_vcvtq_n_u32_v";
  case NEON::BI__builtin_neon_vcvtq_n_u64_v:
    return "NEON::BI__builtin_neon_vcvtq_n_u64_v";
  case NEON::BI__builtin_neon_vcvtx_f32_v:
    return "NEON::BI__builtin_neon_vcvtx_f32_v";
  case NEON::BI__builtin_neon_vdot_s32:
    return "NEON::BI__builtin_neon_vdot_s32";
  case NEON::BI__builtin_neon_vdot_u32:
    return "NEON::BI__builtin_neon_vdot_u32";
  case NEON::BI__builtin_neon_vdotq_s32:
    return "NEON::BI__builtin_neon_vdotq_s32";
  case NEON::BI__builtin_neon_vdotq_u32:
    return "NEON::BI__builtin_neon_vdotq_u32";
  case NEON::BI__builtin_neon_veor3q_s16:
    return "NEON::BI__builtin_neon_veor3q_s16";
  case NEON::BI__builtin_neon_veor3q_s32:
    return "NEON::BI__builtin_neon_veor3q_s32";
  case NEON::BI__builtin_neon_veor3q_s64:
    return "NEON::BI__builtin_neon_veor3q_s64";
  case NEON::BI__builtin_neon_veor3q_s8:
    return "NEON::BI__builtin_neon_veor3q_s8";
  case NEON::BI__builtin_neon_veor3q_u16:
    return "NEON::BI__builtin_neon_veor3q_u16";
  case NEON::BI__builtin_neon_veor3q_u32:
    return "NEON::BI__builtin_neon_veor3q_u32";
  case NEON::BI__builtin_neon_veor3q_u64:
    return "NEON::BI__builtin_neon_veor3q_u64";
  case NEON::BI__builtin_neon_veor3q_u8:
    return "NEON::BI__builtin_neon_veor3q_u8";
  case NEON::BI__builtin_neon_vext_v:
    return "NEON::BI__builtin_neon_vext_v";
  case NEON::BI__builtin_neon_vextq_v:
    return "NEON::BI__builtin_neon_vextq_v";
  case NEON::BI__builtin_neon_vfma_v:
    return "NEON::BI__builtin_neon_vfma_v";
  case NEON::BI__builtin_neon_vfmaq_v:
    return "NEON::BI__builtin_neon_vfmaq_v";
  case NEON::BI__builtin_neon_vfmlal_high_f16:
    return "NEON::BI__builtin_neon_vfmlal_high_f16";
  case NEON::BI__builtin_neon_vfmlal_low_f16:
    return "NEON::BI__builtin_neon_vfmlal_low_f16";
  case NEON::BI__builtin_neon_vfmlalq_high_f16:
    return "NEON::BI__builtin_neon_vfmlalq_high_f16";
  case NEON::BI__builtin_neon_vfmlalq_low_f16:
    return "NEON::BI__builtin_neon_vfmlalq_low_f16";
  case NEON::BI__builtin_neon_vfmlsl_high_f16:
    return "NEON::BI__builtin_neon_vfmlsl_high_f16";
  case NEON::BI__builtin_neon_vfmlsl_low_f16:
    return "NEON::BI__builtin_neon_vfmlsl_low_f16";
  case NEON::BI__builtin_neon_vfmlslq_high_f16:
    return "NEON::BI__builtin_neon_vfmlslq_high_f16";
  case NEON::BI__builtin_neon_vfmlslq_low_f16:
    return "NEON::BI__builtin_neon_vfmlslq_low_f16";
  case NEON::BI__builtin_neon_vhadd_v:
    return "NEON::BI__builtin_neon_vhadd_v";
  case NEON::BI__builtin_neon_vhaddq_v:
    return "NEON::BI__builtin_neon_vhaddq_v";
  case NEON::BI__builtin_neon_vhsub_v:
    return "NEON::BI__builtin_neon_vhsub_v";
  case NEON::BI__builtin_neon_vhsubq_v:
    return "NEON::BI__builtin_neon_vhsubq_v";
  case NEON::BI__builtin_neon_vld1_x2_v:
    return "NEON::BI__builtin_neon_vld1_x2_v";
  case NEON::BI__builtin_neon_vld1_x3_v:
    return "NEON::BI__builtin_neon_vld1_x3_v";
  case NEON::BI__builtin_neon_vld1_x4_v:
    return "NEON::BI__builtin_neon_vld1_x4_v";
  case NEON::BI__builtin_neon_vld1q_x2_v:
    return "NEON::BI__builtin_neon_vld1q_x2_v";
  case NEON::BI__builtin_neon_vld1q_x3_v:
    return "NEON::BI__builtin_neon_vld1q_x3_v";
  case NEON::BI__builtin_neon_vld1q_x4_v:
    return "NEON::BI__builtin_neon_vld1q_x4_v";
  case NEON::BI__builtin_neon_vmmlaq_s32:
    return "NEON::BI__builtin_neon_vmmlaq_s32";
  case NEON::BI__builtin_neon_vmmlaq_u32:
    return "NEON::BI__builtin_neon_vmmlaq_u32";
  case NEON::BI__builtin_neon_vmovl_v:
    return "NEON::BI__builtin_neon_vmovl_v";
  case NEON::BI__builtin_neon_vmovn_v:
    return "NEON::BI__builtin_neon_vmovn_v";
  case NEON::BI__builtin_neon_vmul_v:
    return "NEON::BI__builtin_neon_vmul_v";
  case NEON::BI__builtin_neon_vmulq_v:
    return "NEON::BI__builtin_neon_vmulq_v";
  case NEON::BI__builtin_neon_vpadd_v:
    return "NEON::BI__builtin_neon_vpadd_v";
  case NEON::BI__builtin_neon_vpaddl_v:
    return "NEON::BI__builtin_neon_vpaddl_v";
  case NEON::BI__builtin_neon_vpaddlq_v:
    return "NEON::BI__builtin_neon_vpaddlq_v";
  case NEON::BI__builtin_neon_vpaddq_v:
    return "NEON::BI__builtin_neon_vpaddq_v";
  case NEON::BI__builtin_neon_vqabs_v:
    return "NEON::BI__builtin_neon_vqabs_v";
  case NEON::BI__builtin_neon_vqabsq_v:
    return "NEON::BI__builtin_neon_vqabsq_v";
  case NEON::BI__builtin_neon_vqadd_v:
    return "NEON::BI__builtin_neon_vqadd_v";
  case NEON::BI__builtin_neon_vqaddq_v:
    return "NEON::BI__builtin_neon_vqaddq_v";
  case NEON::BI__builtin_neon_vqdmlal_v:
    return "NEON::BI__builtin_neon_vqdmlal_v";
  case NEON::BI__builtin_neon_vqdmlsl_v:
    return "NEON::BI__builtin_neon_vqdmlsl_v";
  case NEON::BI__builtin_neon_vqdmulh_lane_v:
    return "NEON::BI__builtin_neon_vqdmulh_lane_v";
  case NEON::BI__builtin_neon_vqdmulh_laneq_v:
    return "NEON::BI__builtin_neon_vqdmulh_laneq_v";
  case NEON::BI__builtin_neon_vqdmulh_v:
    return "NEON::BI__builtin_neon_vqdmulh_v";
  case NEON::BI__builtin_neon_vqdmulhq_lane_v:
    return "NEON::BI__builtin_neon_vqdmulhq_lane_v";
  case NEON::BI__builtin_neon_vqdmulhq_laneq_v:
    return "NEON::BI__builtin_neon_vqdmulhq_laneq_v";
  case NEON::BI__builtin_neon_vqdmulhq_v:
    return "NEON::BI__builtin_neon_vqdmulhq_v";
  case NEON::BI__builtin_neon_vqdmull_v:
    return "NEON::BI__builtin_neon_vqdmull_v";
  case NEON::BI__builtin_neon_vqmovn_v:
    return "NEON::BI__builtin_neon_vqmovn_v";
  case NEON::BI__builtin_neon_vqmovun_v:
    return "NEON::BI__builtin_neon_vqmovun_v";
  case NEON::BI__builtin_neon_vqneg_v:
    return "NEON::BI__builtin_neon_vqneg_v";
  case NEON::BI__builtin_neon_vqnegq_v:
    return "NEON::BI__builtin_neon_vqnegq_v";
  case NEON::BI__builtin_neon_vqrdmlah_s16:
    return "NEON::BI__builtin_neon_vqrdmlah_s16";
  case NEON::BI__builtin_neon_vqrdmlah_s32:
    return "NEON::BI__builtin_neon_vqrdmlah_s32";
  case NEON::BI__builtin_neon_vqrdmlahq_s16:
    return "NEON::BI__builtin_neon_vqrdmlahq_s16";
  case NEON::BI__builtin_neon_vqrdmlahq_s32:
    return "NEON::BI__builtin_neon_vqrdmlahq_s32";
  case NEON::BI__builtin_neon_vqrdmlsh_s16:
    return "NEON::BI__builtin_neon_vqrdmlsh_s16";
  case NEON::BI__builtin_neon_vqrdmlsh_s32:
    return "NEON::BI__builtin_neon_vqrdmlsh_s32";
  case NEON::BI__builtin_neon_vqrdmlshq_s16:
    return "NEON::BI__builtin_neon_vqrdmlshq_s16";
  case NEON::BI__builtin_neon_vqrdmlshq_s32:
    return "NEON::BI__builtin_neon_vqrdmlshq_s32";
  case NEON::BI__builtin_neon_vqrdmulh_lane_v:
    return "NEON::BI__builtin_neon_vqrdmulh_lane_v";
  case NEON::BI__builtin_neon_vqrdmulh_laneq_v:
    return "NEON::BI__builtin_neon_vqrdmulh_laneq_v";
  case NEON::BI__builtin_neon_vqrdmulh_v:
    return "NEON::BI__builtin_neon_vqrdmulh_v";
  case NEON::BI__builtin_neon_vqrdmulhq_lane_v:
    return "NEON::BI__builtin_neon_vqrdmulhq_lane_v";
  case NEON::BI__builtin_neon_vqrdmulhq_laneq_v:
    return "NEON::BI__builtin_neon_vqrdmulhq_laneq_v";
  case NEON::BI__builtin_neon_vqrdmulhq_v:
    return "NEON::BI__builtin_neon_vqrdmulhq_v";
  case NEON::BI__builtin_neon_vqrshl_v:
    return "NEON::BI__builtin_neon_vqrshl_v";
  case NEON::BI__builtin_neon_vqrshlq_v:
    return "NEON::BI__builtin_neon_vqrshlq_v";
  case NEON::BI__builtin_neon_vqshl_n_v:
    return "NEON::BI__builtin_neon_vqshl_n_v";
  case NEON::BI__builtin_neon_vqshl_v:
    return "NEON::BI__builtin_neon_vqshl_v";
  case NEON::BI__builtin_neon_vqshlq_n_v:
    return "NEON::BI__builtin_neon_vqshlq_n_v";
  case NEON::BI__builtin_neon_vqshlq_v:
    return "NEON::BI__builtin_neon_vqshlq_v";
  case NEON::BI__builtin_neon_vqshlu_n_v:
    return "NEON::BI__builtin_neon_vqshlu_n_v";
  case NEON::BI__builtin_neon_vqshluq_n_v:
    return "NEON::BI__builtin_neon_vqshluq_n_v";
  case NEON::BI__builtin_neon_vqsub_v:
    return "NEON::BI__builtin_neon_vqsub_v";
  case NEON::BI__builtin_neon_vqsubq_v:
    return "NEON::BI__builtin_neon_vqsubq_v";
  case NEON::BI__builtin_neon_vraddhn_v:
    return "NEON::BI__builtin_neon_vraddhn_v";
  case NEON::BI__builtin_neon_vrax1q_u64:
    return "NEON::BI__builtin_neon_vrax1q_u64";
  case NEON::BI__builtin_neon_vrecpe_v:
    return "NEON::BI__builtin_neon_vrecpe_v";
  case NEON::BI__builtin_neon_vrecpeq_v:
    return "NEON::BI__builtin_neon_vrecpeq_v";
  case NEON::BI__builtin_neon_vrecps_v:
    return "NEON::BI__builtin_neon_vrecps_v";
  case NEON::BI__builtin_neon_vrecpsq_v:
    return "NEON::BI__builtin_neon_vrecpsq_v";
  case NEON::BI__builtin_neon_vrhadd_v:
    return "NEON::BI__builtin_neon_vrhadd_v";
  case NEON::BI__builtin_neon_vrhaddq_v:
    return "NEON::BI__builtin_neon_vrhaddq_v";
  case NEON::BI__builtin_neon_vrnd32x_f32:
    return "NEON::BI__builtin_neon_vrnd32x_f32";
  case NEON::BI__builtin_neon_vrnd32x_f64:
    return "NEON::BI__builtin_neon_vrnd32x_f64";
  case NEON::BI__builtin_neon_vrnd32xq_f32:
    return "NEON::BI__builtin_neon_vrnd32xq_f32";
  case NEON::BI__builtin_neon_vrnd32xq_f64:
    return "NEON::BI__builtin_neon_vrnd32xq_f64";
  case NEON::BI__builtin_neon_vrnd32z_f32:
    return "NEON::BI__builtin_neon_vrnd32z_f32";
  case NEON::BI__builtin_neon_vrnd32z_f64:
    return "NEON::BI__builtin_neon_vrnd32z_f64";
  case NEON::BI__builtin_neon_vrnd32zq_f32:
    return "NEON::BI__builtin_neon_vrnd32zq_f32";
  case NEON::BI__builtin_neon_vrnd32zq_f64:
    return "NEON::BI__builtin_neon_vrnd32zq_f64";
  case NEON::BI__builtin_neon_vrnd64x_f32:
    return "NEON::BI__builtin_neon_vrnd64x_f32";
  case NEON::BI__builtin_neon_vrnd64x_f64:
    return "NEON::BI__builtin_neon_vrnd64x_f64";
  case NEON::BI__builtin_neon_vrnd64xq_f32:
    return "NEON::BI__builtin_neon_vrnd64xq_f32";
  case NEON::BI__builtin_neon_vrnd64xq_f64:
    return "NEON::BI__builtin_neon_vrnd64xq_f64";
  case NEON::BI__builtin_neon_vrnd64z_f32:
    return "NEON::BI__builtin_neon_vrnd64z_f32";
  case NEON::BI__builtin_neon_vrnd64z_f64:
    return "NEON::BI__builtin_neon_vrnd64z_f64";
  case NEON::BI__builtin_neon_vrnd64zq_f32:
    return "NEON::BI__builtin_neon_vrnd64zq_f32";
  case NEON::BI__builtin_neon_vrnd64zq_f64:
    return "NEON::BI__builtin_neon_vrnd64zq_f64";
  case NEON::BI__builtin_neon_vrndi_v:
    return "NEON::BI__builtin_neon_vrndi_v";
  case NEON::BI__builtin_neon_vrndiq_v:
    return "NEON::BI__builtin_neon_vrndiq_v";
  case NEON::BI__builtin_neon_vrshl_v:
    return "NEON::BI__builtin_neon_vrshl_v";
  case NEON::BI__builtin_neon_vrshlq_v:
    return "NEON::BI__builtin_neon_vrshlq_v";
  case NEON::BI__builtin_neon_vrshr_n_v:
    return "NEON::BI__builtin_neon_vrshr_n_v";
  case NEON::BI__builtin_neon_vrshrq_n_v:
    return "NEON::BI__builtin_neon_vrshrq_n_v";
  case NEON::BI__builtin_neon_vrsqrte_v:
    return "NEON::BI__builtin_neon_vrsqrte_v";
  case NEON::BI__builtin_neon_vrsqrteq_v:
    return "NEON::BI__builtin_neon_vrsqrteq_v";
  case NEON::BI__builtin_neon_vrsqrts_v:
    return "NEON::BI__builtin_neon_vrsqrts_v";
  case NEON::BI__builtin_neon_vrsqrtsq_v:
    return "NEON::BI__builtin_neon_vrsqrtsq_v";
  case NEON::BI__builtin_neon_vrsubhn_v:
    return "NEON::BI__builtin_neon_vrsubhn_v";
  case NEON::BI__builtin_neon_vsha1su0q_u32:
    return "NEON::BI__builtin_neon_vsha1su0q_u32";
  case NEON::BI__builtin_neon_vsha1su1q_u32:
    return "NEON::BI__builtin_neon_vsha1su1q_u32";
  case NEON::BI__builtin_neon_vsha256h2q_u32:
    return "NEON::BI__builtin_neon_vsha256h2q_u32";
  case NEON::BI__builtin_neon_vsha256hq_u32:
    return "NEON::BI__builtin_neon_vsha256hq_u32";
  case NEON::BI__builtin_neon_vsha256su0q_u32:
    return "NEON::BI__builtin_neon_vsha256su0q_u32";
  case NEON::BI__builtin_neon_vsha256su1q_u32:
    return "NEON::BI__builtin_neon_vsha256su1q_u32";
  case NEON::BI__builtin_neon_vsha512h2q_u64:
    return "NEON::BI__builtin_neon_vsha512h2q_u64";
  case NEON::BI__builtin_neon_vsha512hq_u64:
    return "NEON::BI__builtin_neon_vsha512hq_u64";
  case NEON::BI__builtin_neon_vsha512su0q_u64:
    return "NEON::BI__builtin_neon_vsha512su0q_u64";
  case NEON::BI__builtin_neon_vsha512su1q_u64:
    return "NEON::BI__builtin_neon_vsha512su1q_u64";
  case NEON::BI__builtin_neon_vshl_n_v:
    return "NEON::BI__builtin_neon_vshl_n_v";
  case NEON::BI__builtin_neon_vshl_v:
    return "NEON::BI__builtin_neon_vshl_v";
  case NEON::BI__builtin_neon_vshll_n_v:
    return "NEON::BI__builtin_neon_vshll_n_v";
  case NEON::BI__builtin_neon_vshlq_n_v:
    return "NEON::BI__builtin_neon_vshlq_n_v";
  case NEON::BI__builtin_neon_vshlq_v:
    return "NEON::BI__builtin_neon_vshlq_v";
  case NEON::BI__builtin_neon_vshr_n_v:
    return "NEON::BI__builtin_neon_vshr_n_v";
  case NEON::BI__builtin_neon_vshrn_n_v:
    return "NEON::BI__builtin_neon_vshrn_n_v";
  case NEON::BI__builtin_neon_vshrq_n_v:
    return "NEON::BI__builtin_neon_vshrq_n_v";
  case NEON::BI__builtin_neon_vsm3partw1q_u32:
    return "NEON::BI__builtin_neon_vsm3partw1q_u32";
  case NEON::BI__builtin_neon_vsm3partw2q_u32:
    return "NEON::BI__builtin_neon_vsm3partw2q_u32";
  case NEON::BI__builtin_neon_vsm3ss1q_u32:
    return "NEON::BI__builtin_neon_vsm3ss1q_u32";
  case NEON::BI__builtin_neon_vsm3tt1aq_u32:
    return "NEON::BI__builtin_neon_vsm3tt1aq_u32";
  case NEON::BI__builtin_neon_vsm3tt1bq_u32:
    return "NEON::BI__builtin_neon_vsm3tt1bq_u32";
  case NEON::BI__builtin_neon_vsm3tt2aq_u32:
    return "NEON::BI__builtin_neon_vsm3tt2aq_u32";
  case NEON::BI__builtin_neon_vsm3tt2bq_u32:
    return "NEON::BI__builtin_neon_vsm3tt2bq_u32";
  case NEON::BI__builtin_neon_vsm4ekeyq_u32:
    return "NEON::BI__builtin_neon_vsm4ekeyq_u32";
  case NEON::BI__builtin_neon_vsm4eq_u32:
    return "NEON::BI__builtin_neon_vsm4eq_u32";
  case NEON::BI__builtin_neon_vst1_x2_v:
    return "NEON::BI__builtin_neon_vst1_x2_v";
  case NEON::BI__builtin_neon_vst1_x3_v:
    return "NEON::BI__builtin_neon_vst1_x3_v";
  case NEON::BI__builtin_neon_vst1_x4_v:
    return "NEON::BI__builtin_neon_vst1_x4_v";
  case NEON::BI__builtin_neon_vst1q_x2_v:
    return "NEON::BI__builtin_neon_vst1q_x2_v";
  case NEON::BI__builtin_neon_vst1q_x3_v:
    return "NEON::BI__builtin_neon_vst1q_x3_v";
  case NEON::BI__builtin_neon_vst1q_x4_v:
    return "NEON::BI__builtin_neon_vst1q_x4_v";
  case NEON::BI__builtin_neon_vsubhn_v:
    return "NEON::BI__builtin_neon_vsubhn_v";
  case NEON::BI__builtin_neon_vtst_v:
    return "NEON::BI__builtin_neon_vtst_v";
  case NEON::BI__builtin_neon_vtstq_v:
    return "NEON::BI__builtin_neon_vtstq_v";
  case NEON::BI__builtin_neon_vusdot_s32:
    return "NEON::BI__builtin_neon_vusdot_s32";
  case NEON::BI__builtin_neon_vusdotq_s32:
    return "NEON::BI__builtin_neon_vusdotq_s32";
  case NEON::BI__builtin_neon_vusmmlaq_s32:
    return "NEON::BI__builtin_neon_vusmmlaq_s32";
  case NEON::BI__builtin_neon_vxarq_u64:
    return "NEON::BI__builtin_neon_vxarq_u64";
  }
}

// Some intrinsics are equivalent for codegen.
static const std::pair<unsigned, unsigned> NEONEquivalentIntrinsicMap[] = {
    {
        NEON::BI__builtin_neon_splat_lane_bf16,
        NEON::BI__builtin_neon_splat_lane_v,
    },
    {
        NEON::BI__builtin_neon_splat_laneq_bf16,
        NEON::BI__builtin_neon_splat_laneq_v,
    },
    {
        NEON::BI__builtin_neon_splatq_lane_bf16,
        NEON::BI__builtin_neon_splatq_lane_v,
    },
    {
        NEON::BI__builtin_neon_splatq_laneq_bf16,
        NEON::BI__builtin_neon_splatq_laneq_v,
    },
    {
        NEON::BI__builtin_neon_vabd_f16,
        NEON::BI__builtin_neon_vabd_v,
    },
    {
        NEON::BI__builtin_neon_vabdq_f16,
        NEON::BI__builtin_neon_vabdq_v,
    },
    {
        NEON::BI__builtin_neon_vabs_f16,
        NEON::BI__builtin_neon_vabs_v,
    },
    {
        NEON::BI__builtin_neon_vabsq_f16,
        NEON::BI__builtin_neon_vabsq_v,
    },
    {
        NEON::BI__builtin_neon_vcage_f16,
        NEON::BI__builtin_neon_vcage_v,
    },
    {
        NEON::BI__builtin_neon_vcageq_f16,
        NEON::BI__builtin_neon_vcageq_v,
    },
    {
        NEON::BI__builtin_neon_vcagt_f16,
        NEON::BI__builtin_neon_vcagt_v,
    },
    {
        NEON::BI__builtin_neon_vcagtq_f16,
        NEON::BI__builtin_neon_vcagtq_v,
    },
    {
        NEON::BI__builtin_neon_vcale_f16,
        NEON::BI__builtin_neon_vcale_v,
    },
    {
        NEON::BI__builtin_neon_vcaleq_f16,
        NEON::BI__builtin_neon_vcaleq_v,
    },
    {
        NEON::BI__builtin_neon_vcalt_f16,
        NEON::BI__builtin_neon_vcalt_v,
    },
    {
        NEON::BI__builtin_neon_vcaltq_f16,
        NEON::BI__builtin_neon_vcaltq_v,
    },
    {
        NEON::BI__builtin_neon_vceqz_f16,
        NEON::BI__builtin_neon_vceqz_v,
    },
    {
        NEON::BI__builtin_neon_vceqzq_f16,
        NEON::BI__builtin_neon_vceqzq_v,
    },
    {
        NEON::BI__builtin_neon_vcgez_f16,
        NEON::BI__builtin_neon_vcgez_v,
    },
    {
        NEON::BI__builtin_neon_vcgezq_f16,
        NEON::BI__builtin_neon_vcgezq_v,
    },
    {
        NEON::BI__builtin_neon_vcgtz_f16,
        NEON::BI__builtin_neon_vcgtz_v,
    },
    {
        NEON::BI__builtin_neon_vcgtzq_f16,
        NEON::BI__builtin_neon_vcgtzq_v,
    },
    {
        NEON::BI__builtin_neon_vclez_f16,
        NEON::BI__builtin_neon_vclez_v,
    },
    {
        NEON::BI__builtin_neon_vclezq_f16,
        NEON::BI__builtin_neon_vclezq_v,
    },
    {
        NEON::BI__builtin_neon_vcltz_f16,
        NEON::BI__builtin_neon_vcltz_v,
    },
    {
        NEON::BI__builtin_neon_vcltzq_f16,
        NEON::BI__builtin_neon_vcltzq_v,
    },
    {
        NEON::BI__builtin_neon_vfma_f16,
        NEON::BI__builtin_neon_vfma_v,
    },
    {
        NEON::BI__builtin_neon_vfma_lane_f16,
        NEON::BI__builtin_neon_vfma_lane_v,
    },
    {
        NEON::BI__builtin_neon_vfma_laneq_f16,
        NEON::BI__builtin_neon_vfma_laneq_v,
    },
    {
        NEON::BI__builtin_neon_vfmaq_f16,
        NEON::BI__builtin_neon_vfmaq_v,
    },
    {
        NEON::BI__builtin_neon_vfmaq_lane_f16,
        NEON::BI__builtin_neon_vfmaq_lane_v,
    },
    {
        NEON::BI__builtin_neon_vfmaq_laneq_f16,
        NEON::BI__builtin_neon_vfmaq_laneq_v,
    },
    {NEON::BI__builtin_neon_vld1_bf16_x2, NEON::BI__builtin_neon_vld1_x2_v},
    {NEON::BI__builtin_neon_vld1_bf16_x3, NEON::BI__builtin_neon_vld1_x3_v},
    {NEON::BI__builtin_neon_vld1_bf16_x4, NEON::BI__builtin_neon_vld1_x4_v},
    {NEON::BI__builtin_neon_vld1_bf16, NEON::BI__builtin_neon_vld1_v},
    {NEON::BI__builtin_neon_vld1_dup_bf16, NEON::BI__builtin_neon_vld1_dup_v},
    {NEON::BI__builtin_neon_vld1_lane_bf16, NEON::BI__builtin_neon_vld1_lane_v},
    {NEON::BI__builtin_neon_vld1q_bf16_x2, NEON::BI__builtin_neon_vld1q_x2_v},
    {NEON::BI__builtin_neon_vld1q_bf16_x3, NEON::BI__builtin_neon_vld1q_x3_v},
    {NEON::BI__builtin_neon_vld1q_bf16_x4, NEON::BI__builtin_neon_vld1q_x4_v},
    {NEON::BI__builtin_neon_vld1q_bf16, NEON::BI__builtin_neon_vld1q_v},
    {NEON::BI__builtin_neon_vld1q_dup_bf16, NEON::BI__builtin_neon_vld1q_dup_v},
    {NEON::BI__builtin_neon_vld1q_lane_bf16,
     NEON::BI__builtin_neon_vld1q_lane_v},
    {NEON::BI__builtin_neon_vld2_bf16, NEON::BI__builtin_neon_vld2_v},
    {NEON::BI__builtin_neon_vld2_dup_bf16, NEON::BI__builtin_neon_vld2_dup_v},
    {NEON::BI__builtin_neon_vld2_lane_bf16, NEON::BI__builtin_neon_vld2_lane_v},
    {NEON::BI__builtin_neon_vld2q_bf16, NEON::BI__builtin_neon_vld2q_v},
    {NEON::BI__builtin_neon_vld2q_dup_bf16, NEON::BI__builtin_neon_vld2q_dup_v},
    {NEON::BI__builtin_neon_vld2q_lane_bf16,
     NEON::BI__builtin_neon_vld2q_lane_v},
    {NEON::BI__builtin_neon_vld3_bf16, NEON::BI__builtin_neon_vld3_v},
    {NEON::BI__builtin_neon_vld3_dup_bf16, NEON::BI__builtin_neon_vld3_dup_v},
    {NEON::BI__builtin_neon_vld3_lane_bf16, NEON::BI__builtin_neon_vld3_lane_v},
    {NEON::BI__builtin_neon_vld3q_bf16, NEON::BI__builtin_neon_vld3q_v},
    {NEON::BI__builtin_neon_vld3q_dup_bf16, NEON::BI__builtin_neon_vld3q_dup_v},
    {NEON::BI__builtin_neon_vld3q_lane_bf16,
     NEON::BI__builtin_neon_vld3q_lane_v},
    {NEON::BI__builtin_neon_vld4_bf16, NEON::BI__builtin_neon_vld4_v},
    {NEON::BI__builtin_neon_vld4_dup_bf16, NEON::BI__builtin_neon_vld4_dup_v},
    {NEON::BI__builtin_neon_vld4_lane_bf16, NEON::BI__builtin_neon_vld4_lane_v},
    {NEON::BI__builtin_neon_vld4q_bf16, NEON::BI__builtin_neon_vld4q_v},
    {NEON::BI__builtin_neon_vld4q_dup_bf16, NEON::BI__builtin_neon_vld4q_dup_v},
    {NEON::BI__builtin_neon_vld4q_lane_bf16,
     NEON::BI__builtin_neon_vld4q_lane_v},
    {
        NEON::BI__builtin_neon_vmax_f16,
        NEON::BI__builtin_neon_vmax_v,
    },
    {
        NEON::BI__builtin_neon_vmaxnm_f16,
        NEON::BI__builtin_neon_vmaxnm_v,
    },
    {
        NEON::BI__builtin_neon_vmaxnmq_f16,
        NEON::BI__builtin_neon_vmaxnmq_v,
    },
    {
        NEON::BI__builtin_neon_vmaxq_f16,
        NEON::BI__builtin_neon_vmaxq_v,
    },
    {
        NEON::BI__builtin_neon_vmin_f16,
        NEON::BI__builtin_neon_vmin_v,
    },
    {
        NEON::BI__builtin_neon_vminnm_f16,
        NEON::BI__builtin_neon_vminnm_v,
    },
    {
        NEON::BI__builtin_neon_vminnmq_f16,
        NEON::BI__builtin_neon_vminnmq_v,
    },
    {
        NEON::BI__builtin_neon_vminq_f16,
        NEON::BI__builtin_neon_vminq_v,
    },
    {
        NEON::BI__builtin_neon_vmulx_f16,
        NEON::BI__builtin_neon_vmulx_v,
    },
    {
        NEON::BI__builtin_neon_vmulxq_f16,
        NEON::BI__builtin_neon_vmulxq_v,
    },
    {
        NEON::BI__builtin_neon_vpadd_f16,
        NEON::BI__builtin_neon_vpadd_v,
    },
    {
        NEON::BI__builtin_neon_vpaddq_f16,
        NEON::BI__builtin_neon_vpaddq_v,
    },
    {
        NEON::BI__builtin_neon_vpmax_f16,
        NEON::BI__builtin_neon_vpmax_v,
    },
    {
        NEON::BI__builtin_neon_vpmaxnm_f16,
        NEON::BI__builtin_neon_vpmaxnm_v,
    },
    {
        NEON::BI__builtin_neon_vpmaxnmq_f16,
        NEON::BI__builtin_neon_vpmaxnmq_v,
    },
    {
        NEON::BI__builtin_neon_vpmaxq_f16,
        NEON::BI__builtin_neon_vpmaxq_v,
    },
    {
        NEON::BI__builtin_neon_vpmin_f16,
        NEON::BI__builtin_neon_vpmin_v,
    },
    {
        NEON::BI__builtin_neon_vpminnm_f16,
        NEON::BI__builtin_neon_vpminnm_v,
    },
    {
        NEON::BI__builtin_neon_vpminnmq_f16,
        NEON::BI__builtin_neon_vpminnmq_v,
    },
    {
        NEON::BI__builtin_neon_vpminq_f16,
        NEON::BI__builtin_neon_vpminq_v,
    },
    {
        NEON::BI__builtin_neon_vrecpe_f16,
        NEON::BI__builtin_neon_vrecpe_v,
    },
    {
        NEON::BI__builtin_neon_vrecpeq_f16,
        NEON::BI__builtin_neon_vrecpeq_v,
    },
    {
        NEON::BI__builtin_neon_vrecps_f16,
        NEON::BI__builtin_neon_vrecps_v,
    },
    {
        NEON::BI__builtin_neon_vrecpsq_f16,
        NEON::BI__builtin_neon_vrecpsq_v,
    },
    {
        NEON::BI__builtin_neon_vrnd_f16,
        NEON::BI__builtin_neon_vrnd_v,
    },
    {
        NEON::BI__builtin_neon_vrnda_f16,
        NEON::BI__builtin_neon_vrnda_v,
    },
    {
        NEON::BI__builtin_neon_vrndaq_f16,
        NEON::BI__builtin_neon_vrndaq_v,
    },
    {
        NEON::BI__builtin_neon_vrndi_f16,
        NEON::BI__builtin_neon_vrndi_v,
    },
    {
        NEON::BI__builtin_neon_vrndiq_f16,
        NEON::BI__builtin_neon_vrndiq_v,
    },
    {
        NEON::BI__builtin_neon_vrndm_f16,
        NEON::BI__builtin_neon_vrndm_v,
    },
    {
        NEON::BI__builtin_neon_vrndmq_f16,
        NEON::BI__builtin_neon_vrndmq_v,
    },
    {
        NEON::BI__builtin_neon_vrndn_f16,
        NEON::BI__builtin_neon_vrndn_v,
    },
    {
        NEON::BI__builtin_neon_vrndnq_f16,
        NEON::BI__builtin_neon_vrndnq_v,
    },
    {
        NEON::BI__builtin_neon_vrndp_f16,
        NEON::BI__builtin_neon_vrndp_v,
    },
    {
        NEON::BI__builtin_neon_vrndpq_f16,
        NEON::BI__builtin_neon_vrndpq_v,
    },
    {
        NEON::BI__builtin_neon_vrndq_f16,
        NEON::BI__builtin_neon_vrndq_v,
    },
    {
        NEON::BI__builtin_neon_vrndx_f16,
        NEON::BI__builtin_neon_vrndx_v,
    },
    {
        NEON::BI__builtin_neon_vrndxq_f16,
        NEON::BI__builtin_neon_vrndxq_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrte_f16,
        NEON::BI__builtin_neon_vrsqrte_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrteq_f16,
        NEON::BI__builtin_neon_vrsqrteq_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrts_f16,
        NEON::BI__builtin_neon_vrsqrts_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrtsq_f16,
        NEON::BI__builtin_neon_vrsqrtsq_v,
    },
    {
        NEON::BI__builtin_neon_vsqrt_f16,
        NEON::BI__builtin_neon_vsqrt_v,
    },
    {
        NEON::BI__builtin_neon_vsqrtq_f16,
        NEON::BI__builtin_neon_vsqrtq_v,
    },
    {NEON::BI__builtin_neon_vst1_bf16_x2, NEON::BI__builtin_neon_vst1_x2_v},
    {NEON::BI__builtin_neon_vst1_bf16_x3, NEON::BI__builtin_neon_vst1_x3_v},
    {NEON::BI__builtin_neon_vst1_bf16_x4, NEON::BI__builtin_neon_vst1_x4_v},
    {NEON::BI__builtin_neon_vst1_bf16, NEON::BI__builtin_neon_vst1_v},
    {NEON::BI__builtin_neon_vst1_lane_bf16, NEON::BI__builtin_neon_vst1_lane_v},
    {NEON::BI__builtin_neon_vst1q_bf16_x2, NEON::BI__builtin_neon_vst1q_x2_v},
    {NEON::BI__builtin_neon_vst1q_bf16_x3, NEON::BI__builtin_neon_vst1q_x3_v},
    {NEON::BI__builtin_neon_vst1q_bf16_x4, NEON::BI__builtin_neon_vst1q_x4_v},
    {NEON::BI__builtin_neon_vst1q_bf16, NEON::BI__builtin_neon_vst1q_v},
    {NEON::BI__builtin_neon_vst1q_lane_bf16,
     NEON::BI__builtin_neon_vst1q_lane_v},
    {NEON::BI__builtin_neon_vst2_bf16, NEON::BI__builtin_neon_vst2_v},
    {NEON::BI__builtin_neon_vst2_lane_bf16, NEON::BI__builtin_neon_vst2_lane_v},
    {NEON::BI__builtin_neon_vst2q_bf16, NEON::BI__builtin_neon_vst2q_v},
    {NEON::BI__builtin_neon_vst2q_lane_bf16,
     NEON::BI__builtin_neon_vst2q_lane_v},
    {NEON::BI__builtin_neon_vst3_bf16, NEON::BI__builtin_neon_vst3_v},
    {NEON::BI__builtin_neon_vst3_lane_bf16, NEON::BI__builtin_neon_vst3_lane_v},
    {NEON::BI__builtin_neon_vst3q_bf16, NEON::BI__builtin_neon_vst3q_v},
    {NEON::BI__builtin_neon_vst3q_lane_bf16,
     NEON::BI__builtin_neon_vst3q_lane_v},
    {NEON::BI__builtin_neon_vst4_bf16, NEON::BI__builtin_neon_vst4_v},
    {NEON::BI__builtin_neon_vst4_lane_bf16, NEON::BI__builtin_neon_vst4_lane_v},
    {NEON::BI__builtin_neon_vst4q_bf16, NEON::BI__builtin_neon_vst4q_v},
    {NEON::BI__builtin_neon_vst4q_lane_bf16,
     NEON::BI__builtin_neon_vst4q_lane_v},
    // The mangling rules cause us to have one ID for each type for
    // vldap1(q)_lane and vstl1(q)_lane, but codegen is equivalent for all of
    // them. Choose an arbitrary one to be handled as tha canonical variation.
    {NEON::BI__builtin_neon_vldap1_lane_u64,
     NEON::BI__builtin_neon_vldap1_lane_s64},
    {NEON::BI__builtin_neon_vldap1_lane_f64,
     NEON::BI__builtin_neon_vldap1_lane_s64},
    {NEON::BI__builtin_neon_vldap1_lane_p64,
     NEON::BI__builtin_neon_vldap1_lane_s64},
    {NEON::BI__builtin_neon_vldap1q_lane_u64,
     NEON::BI__builtin_neon_vldap1q_lane_s64},
    {NEON::BI__builtin_neon_vldap1q_lane_f64,
     NEON::BI__builtin_neon_vldap1q_lane_s64},
    {NEON::BI__builtin_neon_vldap1q_lane_p64,
     NEON::BI__builtin_neon_vldap1q_lane_s64},
    {NEON::BI__builtin_neon_vstl1_lane_u64,
     NEON::BI__builtin_neon_vstl1_lane_s64},
    {NEON::BI__builtin_neon_vstl1_lane_f64,
     NEON::BI__builtin_neon_vstl1_lane_s64},
    {NEON::BI__builtin_neon_vstl1_lane_p64,
     NEON::BI__builtin_neon_vstl1_lane_s64},
    {NEON::BI__builtin_neon_vstl1q_lane_u64,
     NEON::BI__builtin_neon_vstl1q_lane_s64},
    {NEON::BI__builtin_neon_vstl1q_lane_f64,
     NEON::BI__builtin_neon_vstl1q_lane_s64},
    {NEON::BI__builtin_neon_vstl1q_lane_p64,
     NEON::BI__builtin_neon_vstl1q_lane_s64},
};

#undef NEONMAP0
#undef NEONMAP1
#undef NEONMAP2

#define SMEMAP1(NameBase, LLVMIntrinsic, TypeModifier)                         \
  {#NameBase, SME::BI__builtin_sme_##NameBase, Intrinsic::LLVMIntrinsic, 0,    \
   TypeModifier}

#define SMEMAP2(NameBase, TypeModifier)                                        \
  {#NameBase, SME::BI__builtin_sme_##NameBase, 0, 0, TypeModifier}
[[maybe_unused]] static const ARMVectorIntrinsicInfo AArch64SMEIntrinsicMap[] = {
#define GET_SME_LLVM_INTRINSIC_MAP
#include "clang/Basic/arm_sme_builtin_cg.inc"
#undef GET_SME_LLVM_INTRINSIC_MAP
};

#undef SMEMAP1
#undef SMEMAP2

static std::optional<CIRGenFunction::MSVCIntrin>
translateAarch64ToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = CIRGenFunction::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case clang::AArch64::BI_BitScanForward:
  case clang::AArch64::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case clang::AArch64::BI_BitScanReverse:
  case clang::AArch64::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case clang::AArch64::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case clang::AArch64::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case clang::AArch64::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case clang::AArch64::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case clang::AArch64::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case clang::AArch64::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case clang::AArch64::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case clang::AArch64::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  case clang::AArch64::BI_InterlockedExchangeAdd8_acq:
  case clang::AArch64::BI_InterlockedExchangeAdd16_acq:
  case clang::AArch64::BI_InterlockedExchangeAdd_acq:
  case clang::AArch64::BI_InterlockedExchangeAdd64_acq:
    return MSVCIntrin::_InterlockedExchangeAdd_acq;
  case clang::AArch64::BI_InterlockedExchangeAdd8_rel:
  case clang::AArch64::BI_InterlockedExchangeAdd16_rel:
  case clang::AArch64::BI_InterlockedExchangeAdd_rel:
  case clang::AArch64::BI_InterlockedExchangeAdd64_rel:
    return MSVCIntrin::_InterlockedExchangeAdd_rel;
  case clang::AArch64::BI_InterlockedExchangeAdd8_nf:
  case clang::AArch64::BI_InterlockedExchangeAdd16_nf:
  case clang::AArch64::BI_InterlockedExchangeAdd_nf:
  case clang::AArch64::BI_InterlockedExchangeAdd64_nf:
    return MSVCIntrin::_InterlockedExchangeAdd_nf;
  case clang::AArch64::BI_InterlockedExchange8_acq:
  case clang::AArch64::BI_InterlockedExchange16_acq:
  case clang::AArch64::BI_InterlockedExchange_acq:
  case clang::AArch64::BI_InterlockedExchange64_acq:
    return MSVCIntrin::_InterlockedExchange_acq;
  case clang::AArch64::BI_InterlockedExchange8_rel:
  case clang::AArch64::BI_InterlockedExchange16_rel:
  case clang::AArch64::BI_InterlockedExchange_rel:
  case clang::AArch64::BI_InterlockedExchange64_rel:
    return MSVCIntrin::_InterlockedExchange_rel;
  case clang::AArch64::BI_InterlockedExchange8_nf:
  case clang::AArch64::BI_InterlockedExchange16_nf:
  case clang::AArch64::BI_InterlockedExchange_nf:
  case clang::AArch64::BI_InterlockedExchange64_nf:
    return MSVCIntrin::_InterlockedExchange_nf;
  case clang::AArch64::BI_InterlockedCompareExchange8_acq:
  case clang::AArch64::BI_InterlockedCompareExchange16_acq:
  case clang::AArch64::BI_InterlockedCompareExchange_acq:
  case clang::AArch64::BI_InterlockedCompareExchange64_acq:
    return MSVCIntrin::_InterlockedCompareExchange_acq;
  case clang::AArch64::BI_InterlockedCompareExchange8_rel:
  case clang::AArch64::BI_InterlockedCompareExchange16_rel:
  case clang::AArch64::BI_InterlockedCompareExchange_rel:
  case clang::AArch64::BI_InterlockedCompareExchange64_rel:
    return MSVCIntrin::_InterlockedCompareExchange_rel;
  case clang::AArch64::BI_InterlockedCompareExchange8_nf:
  case clang::AArch64::BI_InterlockedCompareExchange16_nf:
  case clang::AArch64::BI_InterlockedCompareExchange_nf:
  case clang::AArch64::BI_InterlockedCompareExchange64_nf:
    return MSVCIntrin::_InterlockedCompareExchange_nf;
  case clang::AArch64::BI_InterlockedCompareExchange128:
    return MSVCIntrin::_InterlockedCompareExchange128;
  case clang::AArch64::BI_InterlockedCompareExchange128_acq:
    return MSVCIntrin::_InterlockedCompareExchange128_acq;
  case clang::AArch64::BI_InterlockedCompareExchange128_nf:
    return MSVCIntrin::_InterlockedCompareExchange128_nf;
  case clang::AArch64::BI_InterlockedCompareExchange128_rel:
    return MSVCIntrin::_InterlockedCompareExchange128_rel;
  case clang::AArch64::BI_InterlockedOr8_acq:
  case clang::AArch64::BI_InterlockedOr16_acq:
  case clang::AArch64::BI_InterlockedOr_acq:
  case clang::AArch64::BI_InterlockedOr64_acq:
    return MSVCIntrin::_InterlockedOr_acq;
  case clang::AArch64::BI_InterlockedOr8_rel:
  case clang::AArch64::BI_InterlockedOr16_rel:
  case clang::AArch64::BI_InterlockedOr_rel:
  case clang::AArch64::BI_InterlockedOr64_rel:
    return MSVCIntrin::_InterlockedOr_rel;
  case clang::AArch64::BI_InterlockedOr8_nf:
  case clang::AArch64::BI_InterlockedOr16_nf:
  case clang::AArch64::BI_InterlockedOr_nf:
  case clang::AArch64::BI_InterlockedOr64_nf:
    return MSVCIntrin::_InterlockedOr_nf;
  case clang::AArch64::BI_InterlockedXor8_acq:
  case clang::AArch64::BI_InterlockedXor16_acq:
  case clang::AArch64::BI_InterlockedXor_acq:
  case clang::AArch64::BI_InterlockedXor64_acq:
    return MSVCIntrin::_InterlockedXor_acq;
  case clang::AArch64::BI_InterlockedXor8_rel:
  case clang::AArch64::BI_InterlockedXor16_rel:
  case clang::AArch64::BI_InterlockedXor_rel:
  case clang::AArch64::BI_InterlockedXor64_rel:
    return MSVCIntrin::_InterlockedXor_rel;
  case clang::AArch64::BI_InterlockedXor8_nf:
  case clang::AArch64::BI_InterlockedXor16_nf:
  case clang::AArch64::BI_InterlockedXor_nf:
  case clang::AArch64::BI_InterlockedXor64_nf:
    return MSVCIntrin::_InterlockedXor_nf;
  case clang::AArch64::BI_InterlockedAnd8_acq:
  case clang::AArch64::BI_InterlockedAnd16_acq:
  case clang::AArch64::BI_InterlockedAnd_acq:
  case clang::AArch64::BI_InterlockedAnd64_acq:
    return MSVCIntrin::_InterlockedAnd_acq;
  case clang::AArch64::BI_InterlockedAnd8_rel:
  case clang::AArch64::BI_InterlockedAnd16_rel:
  case clang::AArch64::BI_InterlockedAnd_rel:
  case clang::AArch64::BI_InterlockedAnd64_rel:
    return MSVCIntrin::_InterlockedAnd_rel;
  case clang::AArch64::BI_InterlockedAnd8_nf:
  case clang::AArch64::BI_InterlockedAnd16_nf:
  case clang::AArch64::BI_InterlockedAnd_nf:
  case clang::AArch64::BI_InterlockedAnd64_nf:
    return MSVCIntrin::_InterlockedAnd_nf;
  case clang::AArch64::BI_InterlockedIncrement16_acq:
  case clang::AArch64::BI_InterlockedIncrement_acq:
  case clang::AArch64::BI_InterlockedIncrement64_acq:
    return MSVCIntrin::_InterlockedIncrement_acq;
  case clang::AArch64::BI_InterlockedIncrement16_rel:
  case clang::AArch64::BI_InterlockedIncrement_rel:
  case clang::AArch64::BI_InterlockedIncrement64_rel:
    return MSVCIntrin::_InterlockedIncrement_rel;
  case clang::AArch64::BI_InterlockedIncrement16_nf:
  case clang::AArch64::BI_InterlockedIncrement_nf:
  case clang::AArch64::BI_InterlockedIncrement64_nf:
    return MSVCIntrin::_InterlockedIncrement_nf;
  case clang::AArch64::BI_InterlockedDecrement16_acq:
  case clang::AArch64::BI_InterlockedDecrement_acq:
  case clang::AArch64::BI_InterlockedDecrement64_acq:
    return MSVCIntrin::_InterlockedDecrement_acq;
  case clang::AArch64::BI_InterlockedDecrement16_rel:
  case clang::AArch64::BI_InterlockedDecrement_rel:
  case clang::AArch64::BI_InterlockedDecrement64_rel:
    return MSVCIntrin::_InterlockedDecrement_rel;
  case clang::AArch64::BI_InterlockedDecrement16_nf:
  case clang::AArch64::BI_InterlockedDecrement_nf:
  case clang::AArch64::BI_InterlockedDecrement64_nf:
    return MSVCIntrin::_InterlockedDecrement_nf;
  }
  llvm_unreachable("must return from switch");
}

static bool AArch64SIMDIntrinsicsProvenSorted = false;
static bool AArch64SISDIntrinsicsProvenSorted = false;
[[maybe_unused]] static bool AArch64SVEIntrinsicsProvenSorted = false;
[[maybe_unused]] static bool AArch64SMEIntrinsicsProvenSorted = false;

static const ARMVectorIntrinsicInfo *
findARMVectorIntrinsicInMap(ArrayRef<ARMVectorIntrinsicInfo> IntrinsicMap,
                            unsigned BuiltinID, bool &MapProvenSorted) {

#ifndef NDEBUG
  if (!MapProvenSorted) {
    assert(llvm::is_sorted(IntrinsicMap));
    MapProvenSorted = true;
  }
#endif

  const ARMVectorIntrinsicInfo *Builtin =
      llvm::lower_bound(IntrinsicMap, BuiltinID);

  if (Builtin != IntrinsicMap.end() && Builtin->BuiltinID == BuiltinID)
    return Builtin;

  return nullptr;
}

static cir::VectorType GetNeonType(CIRGenFunction *CGF, NeonTypeFlags TypeFlags,
                                   bool HasLegalHalfType = true,
                                   bool V1Ty = false,
                                   bool AllowBFloatArgsAndRet = true) {
  int IsQuad = TypeFlags.isQuad();
  switch (TypeFlags.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
  case NeonTypeFlags::MFloat8:
    return cir::VectorType::get(TypeFlags.isUnsigned() ? CGF->uInt8Ty
                                                       : CGF->sInt8Ty,
                                V1Ty ? 1 : (8 << IsQuad));
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return cir::VectorType::get(TypeFlags.isUnsigned() ? CGF->uInt16Ty
                                                       : CGF->sInt16Ty,
                                V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::BFloat16:
    if (AllowBFloatArgsAndRet)
      return cir::VectorType::get(
          cir::BF16Type::get(CGF->getBuilder().getContext()),
          V1Ty ? 1 : (4 << IsQuad));
    return cir::VectorType::get(CGF->sInt16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Float16:
    if (HasLegalHalfType)
      return cir::VectorType::get(
          cir::FP16Type::get(CGF->getBuilder().getContext()),
          V1Ty ? 1 : (4 << IsQuad));
    return cir::VectorType::get(CGF->sInt16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return cir::VectorType::get(TypeFlags.isUnsigned() ? CGF->uInt32Ty
                                                       : CGF->sInt32Ty,
                                V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return cir::VectorType::get(TypeFlags.isUnsigned() ? CGF->uInt64Ty
                                                       : CGF->sInt64Ty,
                                V1Ty ? 1 : (1 << IsQuad));
  case NeonTypeFlags::Poly128:
    // FIXME: i128 and f128 doesn't get fully support in Clang and llvm.
    // There is a lot of i128 and f128 API missing.
    // so we use v16i8 to represent poly128 and get pattern matched.
    return cir::VectorType::get(CGF->uInt8Ty, 16);
  case NeonTypeFlags::Float32:
    return cir::VectorType::get(CGF->getCIRGenModule().floatTy,
                                V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Float64:
    return cir::VectorType::get(CGF->getCIRGenModule().doubleTy,
                                V1Ty ? 1 : (1 << IsQuad));
  }
  llvm_unreachable("Unknown vector element type!");
}

static cir::VectorType getSVEPredType(CIRGenFunction *CGF,
                                      const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Unhandled SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
  case SVETypeFlags::EltTyBool8:
    return cir::VectorType::get(CGF->getBuilder().getBoolTy(), 16, true);
  case SVETypeFlags::EltTyInt16:
  case SVETypeFlags::EltTyBFloat16:
  case SVETypeFlags::EltTyFloat16:
  case SVETypeFlags::EltTyBool16:
    return cir::VectorType::get(CGF->getBuilder().getBoolTy(), 8, true);
  case SVETypeFlags::EltTyInt32:
  case SVETypeFlags::EltTyFloat32:
  case SVETypeFlags::EltTyBool32:
    return cir::VectorType::get(CGF->getBuilder().getBoolTy(), 4, true);
  case SVETypeFlags::EltTyInt64:
  case SVETypeFlags::EltTyFloat64:
  case SVETypeFlags::EltTyBool64:
    return cir::VectorType::get(CGF->getBuilder().getBoolTy(), 2, true);
  }
}

// Return the scalable data vector type for an SVE/SME builtin. Mirrors classic
// CodeGenFunction::getSVEType: it maps the ACLE element type to the overloaded
// intrinsic's data type (e.g. nxv4i32 for i32 data). The integer signedness is
// irrelevant to the intrinsic overload suffix (which is width-based), so signed
// integer types are used uniformly.
static cir::VectorType getSVEDataType(CIRGenFunction *CGF,
                                      const SVETypeFlags &TypeFlags) {
  CIRGenBuilderTy &builder = CGF->getBuilder();
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Invalid SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return cir::VectorType::get(builder.getSInt8Ty(), 16, true);
  case SVETypeFlags::EltTyInt16:
    return cir::VectorType::get(builder.getSInt16Ty(), 8, true);
  case SVETypeFlags::EltTyInt32:
    return cir::VectorType::get(builder.getSInt32Ty(), 4, true);
  case SVETypeFlags::EltTyInt64:
    return cir::VectorType::get(builder.getSInt64Ty(), 2, true);

  case SVETypeFlags::EltTyMFloat8:
    return cir::VectorType::get(builder.getSInt8Ty(), 16, true);
  case SVETypeFlags::EltTyFloat16:
    return cir::VectorType::get(CGF->getCIRGenModule().fP16Ty, 8, true);
  case SVETypeFlags::EltTyBFloat16:
    return cir::VectorType::get(CGF->getCIRGenModule().bFloat16Ty, 8, true);
  case SVETypeFlags::EltTyFloat32:
    return cir::VectorType::get(CGF->getCIRGenModule().floatTy, 4, true);
  case SVETypeFlags::EltTyFloat64:
    return cir::VectorType::get(CGF->getCIRGenModule().doubleTy, 2, true);

  case SVETypeFlags::EltTyBool8:
    return cir::VectorType::get(builder.getBoolTy(), 16, true);
  case SVETypeFlags::EltTyBool16:
    return cir::VectorType::get(builder.getBoolTy(), 8, true);
  case SVETypeFlags::EltTyBool32:
    return cir::VectorType::get(builder.getBoolTy(), 4, true);
  case SVETypeFlags::EltTyBool64:
    return cir::VectorType::get(builder.getBoolTy(), 2, true);
  }
}

// Return the SVE scalable vector type whose lane count matches a scalar
// element type (128 bits per SVE block => 128/elementBits lanes). Mirrors
// classic getSVEVectorForElementType, which derives the lane count from
// EltTy->getScalarSizeInBits() and therefore handles both integer and
// floating-point elements. Used to splat a scalar operand (e.g. the scalar of
// an `_n_` intrinsic such as svadd_n_f32, or the svdup_lane lane index).
static cir::VectorType getSVEVectorForElementType(mlir::Type eltTy) {
  unsigned eltBits;
  if (auto intTy = mlir::dyn_cast<cir::IntTypeInterface>(eltTy))
    eltBits = intTy.getWidth();
  else
    eltBits = mlir::cast<cir::FPTypeInterface>(eltTy).getWidth();
  return cir::VectorType::get(eltTy, /*size=*/128 / eltBits,
                              /*isScalable=*/true);
}

static mlir::Value emitAArch64TblBuiltinExpr(CIRGenFunction &CGF,
                                             unsigned BuiltinID,
                                             const CallExpr *E,
                                             SmallVectorImpl<mlir::Value> &Ops,
                                             llvm::Triple::ArchType Arch) {
  unsigned int Int = 0;
  [[maybe_unused]] const char *s = nullptr;

  switch (BuiltinID) {
  default:
    return {};
  case NEON::BI__builtin_neon_vtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
  case NEON::BI__builtin_neon_vtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v:
  case NEON::BI__builtin_neon_vtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
  case NEON::BI__builtin_neon_vtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    break;
  case NEON::BI__builtin_neon_vtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
  case NEON::BI__builtin_neon_vtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
  case NEON::BI__builtin_neon_vtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
  case NEON::BI__builtin_neon_vtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    break;
  }

  assert(E->getNumArgs() >= 3);

  // Get the last argument, which specifies the vector type.
  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  std::optional<llvm::APSInt> Result =
      Arg->getIntegerConstantExpr(CGF.getContext());
  if (!Result)
    return nullptr;

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type = Result->getZExtValue();
  cir::VectorType Ty = GetNeonType(&CGF, Type);
  if (!Ty)
    return nullptr;

  // AArch64 scalar builtins are not overloaded, they do not have an extra
  // argument that specifies the vector type, need to handle each case.
  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vtbl1_v: {
    // vtbl1: Table lookup from 1 table
    // Takes 64-bit table and 64-bit indices, extends table to 128-bit
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());

    // Get the table operand type (may differ from return type due to bitcast)
    auto TblTy = mlir::cast<cir::VectorType>(Ops[0].getType());

    // Extend 64-bit table to 128-bit by shuffling with zero
    // Indices: [0, ..., 15]
    // First 8 from table, last 8 from zero vector
    mlir::Value ZeroVec = Builder.getZero(Loc, TblTy);
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};

    mlir::Value ExtTable =
        Builder.createVecShuffle(Loc, Ops[0], ZeroVec, Indices);

    // Call aarch64.neon.tbl1 intrinsic
    llvm::SmallVector<mlir::Value, 2> Args = {ExtTable, Ops[1]};
    cir::LLVMIntrinsicCallOp Op = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbl1"),
        Ty, Args);
    return Op.getResult();
  }
  case NEON::BI__builtin_neon_vtbl2_v: {
    // vtbl2: Table lookup from 2 tables
    // Merges 2 x 64-bit tables into 1 x 128-bit, then calls tbl1
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};
    mlir::Value Table = Builder.createVecShuffle(Loc, Ops[0], Ops[1], Indices);
    llvm::SmallVector<mlir::Value, 2> Args = {Table, Ops[2]};
    cir::LLVMIntrinsicCallOp Op = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbl1"),
        Ty, Args);
    return Op.getResult();
  }
  case NEON::BI__builtin_neon_vtbl3_v: {
    // vtbl3: Table lookup from 3 tables
    // Merges first 2 tables into 128-bit, zero-extends third to 128-bit,
    // then calls tbl2
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};
    mlir::Value Table1 = Builder.createVecShuffle(Loc, Ops[0], Ops[1], Indices);
    auto TblTy = mlir::cast<cir::VectorType>(Ops[2].getType());
    mlir::Value ZeroVec = Builder.getZero(Loc, TblTy);
    mlir::Value Table2 = Builder.createVecShuffle(Loc, Ops[2], ZeroVec, Indices);
    llvm::SmallVector<mlir::Value, 3> Args = {Table1, Table2, Ops[3]};
    cir::LLVMIntrinsicCallOp Op = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbl2"),
        Ty, Args);
    return Op.getResult();
  }
  case NEON::BI__builtin_neon_vtbl4_v: {
    // vtbl4: Table lookup from 4 tables
    // Takes 4 x 64-bit tables and 64-bit indices
    // Pairs tables into two 128-bit vectors, then calls tbl2
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());

    // Indices for merging two 8-element vectors into one 16-element vector
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};

    // Merge first pair: Ops[0] + Ops[1] -> table1 (128-bit)
    mlir::Value Table1 = Builder.createVecShuffle(Loc, Ops[0], Ops[1], Indices);

    // Merge second pair: Ops[2] + Ops[3] -> table2 (128-bit)
    mlir::Value Table2 = Builder.createVecShuffle(Loc, Ops[2], Ops[3], Indices);

    // Call aarch64.neon.tbl2 intrinsic
    llvm::SmallVector<mlir::Value, 3> Args = {Table1, Table2, Ops[4]};
    cir::LLVMIntrinsicCallOp Op = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbl2"),
        Ty, Args);
    return Op.getResult();
  }
  case NEON::BI__builtin_neon_vtbx1_v: {
    // vtbx1: Table lookup with extend from 1 table
    // For out-of-range indices (>= 8), use original value from Ops[0]
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());

    // Zero-extend table to 128-bit and do tbl1 lookup
    auto TblTy = mlir::cast<cir::VectorType>(Ops[1].getType());
    mlir::Value ZeroVec = Builder.getZero(Loc, TblTy);
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};
    mlir::Value ExtTable =
        Builder.createVecShuffle(Loc, Ops[1], ZeroVec, Indices);
    llvm::SmallVector<mlir::Value, 2> TblArgs = {ExtTable, Ops[2]};
    cir::LLVMIntrinsicCallOp TblOp = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbl1"),
        Ty, TblArgs);
    mlir::Value TblRes = TblOp.getResult();

    // For indices >= 8, use original value: blend via comparison
    auto VecTy = mlir::cast<cir::VectorType>(Ty);
    auto EltTy = mlir::cast<cir::IntType>(VecTy.getElementType());
    cir::IntAttr eightAttr;
    if (EltTy == Builder.getSInt8Ty())
      eightAttr = Builder.getSInt8AttrLocalCached(8);
    else if (EltTy == Builder.getUInt8Ty())
      eightAttr = Builder.getUInt8AttrLocalCached(8);
    else
      eightAttr = cir::IntAttr::get(EltTy, 8);
    mlir::Value Eight = Builder.getConstant(Loc, eightAttr);
    mlir::Value EightV =
        cir::VecSplatOp::create(Builder, Loc, Ty, Eight);
    // The index operand (Ops[2]) is always a SIGNED int8 vector -- arm_neon.h
    // bit-casts the index to int8x8_t for both the s8 and u8 wrappers -- while
    // EightV has the result type Ty, whose element signedness varies by
    // variant (s8 -> signed, u8 -> unsigned). Cast BOTH to a common unsigned
    // vector type so cir.vec.cmp sees matching operand types and the
    // out-of-range check is an unsigned compare (table indices are unsigned).
    // Without the unconditional cast, the u8 variant compared a signed LHS
    // against an unsigned RHS and failed CIR verification.
    auto UnsignedEltTy =
        cir::IntType::get(Builder.getContext(), EltTy.getWidth(), false);
    auto UnsignedVecTy = cir::VectorType::get(UnsignedEltTy, VecTy.getSize());
    mlir::Value CmpLHS = Builder.createIntCast(Ops[2], UnsignedVecTy);
    mlir::Value CmpRHS = Builder.createIntCast(EightV, UnsignedVecTy);
    mlir::Value CmpRes = Builder.createVecCompare(
        Loc, cir::CmpOpKind::ge, CmpLHS, CmpRHS);
    // CmpRes is signed int vec; cast to match Ty for bitwise ops
    CmpRes = Builder.createBitcast(CmpRes, Ty);
    // The original value (Ops[0]) may be a signed int8 vector (arm_neon.h
    // bit-casts it) while Ty/CmpRes are unsigned for the u8 variant; bitcast
    // it to Ty so the blend's bitwise ops have matching operand types.
    mlir::Value OrigVal = Builder.createBitcast(Ops[0], Ty);
    mlir::Value EltsFromInput = Builder.createAnd(Loc, CmpRes, OrigVal);
    mlir::Value NotCmp = Builder.createNot(CmpRes);
    mlir::Value EltsFromTbl = Builder.createAnd(Loc, NotCmp, TblRes);
    return Builder.createOr(Loc, EltsFromInput, EltsFromTbl);
  }
  case NEON::BI__builtin_neon_vtbx2_v: {
    // vtbx2: Table lookup with extend from 2 tables
    // Merge tables into 128-bit, call tbx1 (hardware does blending)
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};
    mlir::Value Table = Builder.createVecShuffle(Loc, Ops[1], Ops[2], Indices);
    llvm::SmallVector<mlir::Value, 3> Args = {Ops[0], Table, Ops[3]};
    cir::LLVMIntrinsicCallOp Op = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbx1"),
        Ty, Args);
    return Op.getResult();
  }
  case NEON::BI__builtin_neon_vtbx3_v: {
    // vtbx3: Table lookup with extend from 3 tables
    // tbl2 lookup, then blend for out-of-range indices (>= 24)
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};
    mlir::Value Table1 = Builder.createVecShuffle(Loc, Ops[1], Ops[2], Indices);
    auto TblTy = mlir::cast<cir::VectorType>(Ops[3].getType());
    mlir::Value ZeroVec = Builder.getZero(Loc, TblTy);
    mlir::Value Table2 = Builder.createVecShuffle(Loc, Ops[3], ZeroVec, Indices);
    llvm::SmallVector<mlir::Value, 3> TblArgs = {Table1, Table2, Ops[4]};
    cir::LLVMIntrinsicCallOp TblOp = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbl2"),
        Ty, TblArgs);
    mlir::Value TblRes = TblOp.getResult();

    // For indices >= 24, use original value
    auto VecTy = mlir::cast<cir::VectorType>(Ty);
    auto EltTy = mlir::cast<cir::IntType>(VecTy.getElementType());
    cir::IntAttr twentyFourAttr;
    if (EltTy == Builder.getSInt8Ty())
      twentyFourAttr = Builder.getSInt8AttrLocalCached(24);
    else if (EltTy == Builder.getUInt8Ty())
      twentyFourAttr = Builder.getUInt8AttrLocalCached(24);
    else
      twentyFourAttr = cir::IntAttr::get(EltTy, 24);
    mlir::Value TwentyFour = Builder.getConstant(Loc, twentyFourAttr);
    mlir::Value TwentyFourV =
        cir::VecSplatOp::create(Builder, Loc, Ty, TwentyFour);
    // Same fix as vtbx1: the index (Ops[4]) is always signed int8 (arm_neon.h
    // bit-casts to int8x8_t for both s8 and u8), while TwentyFourV has type Ty
    // whose signedness varies. Cast BOTH to a common unsigned vector type so
    // cir.vec.cmp operands match and the out-of-range check is unsigned;
    // otherwise the u8 variant fails CIR verification.
    auto UnsignedEltTy =
        cir::IntType::get(Builder.getContext(), EltTy.getWidth(), false);
    auto UnsignedVecTy = cir::VectorType::get(UnsignedEltTy, VecTy.getSize());
    mlir::Value CmpLHS = Builder.createIntCast(Ops[4], UnsignedVecTy);
    mlir::Value CmpRHS = Builder.createIntCast(TwentyFourV, UnsignedVecTy);
    mlir::Value CmpRes = Builder.createVecCompare(
        Loc, cir::CmpOpKind::ge, CmpLHS, CmpRHS);
    CmpRes = Builder.createBitcast(CmpRes, Ty);
    // The original value (Ops[0]) may be a signed int8 vector (arm_neon.h
    // bit-casts it) while Ty/CmpRes are unsigned for the u8 variant; bitcast
    // it to Ty so the blend's bitwise ops have matching operand types.
    mlir::Value OrigVal = Builder.createBitcast(Ops[0], Ty);
    mlir::Value EltsFromInput = Builder.createAnd(Loc, CmpRes, OrigVal);
    mlir::Value NotCmp = Builder.createNot(CmpRes);
    mlir::Value EltsFromTbl = Builder.createAnd(Loc, NotCmp, TblRes);
    return Builder.createOr(Loc, EltsFromInput, EltsFromTbl);
  }
  case NEON::BI__builtin_neon_vtbx4_v: {
    // vtbx4: Table lookup with extend from 4 tables
    // Merge 4 x 64-bit tables into 2 x 128-bit vectors, then call tbx2.
    CIRGenBuilderTy &Builder = CGF.getBuilder();
    mlir::Location Loc = CGF.getLoc(E->getExprLoc());
    llvm::SmallVector<int64_t, 16> Indices = {0, 1, 2,  3,  4,  5,  6,  7,
                                              8, 9, 10, 11, 12, 13, 14, 15};
    mlir::Value Table1 = Builder.createVecShuffle(Loc, Ops[1], Ops[2], Indices);
    mlir::Value Table2 = Builder.createVecShuffle(Loc, Ops[3], Ops[4], Indices);
    llvm::SmallVector<mlir::Value, 4> Args = {Ops[0], Table1, Table2, Ops[5]};
    cir::LLVMIntrinsicCallOp Op = cir::LLVMIntrinsicCallOp::create(
        Builder, Loc, CGF.getCIRGenModule().getStringAttrCached("aarch64.neon.tbx2"),
        Ty, Args);
    return Op.getResult();
  }
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
    Int = Intrinsic::aarch64_neon_tbl1;
    s = "vtbl1";
    break;
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    Int = Intrinsic::aarch64_neon_tbl2;
    s = "vtbl2";
    break;
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
    Int = Intrinsic::aarch64_neon_tbl3;
    s = "vtbl3";
    break;
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    Int = Intrinsic::aarch64_neon_tbl4;
    s = "vtbl4";
    break;
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
    Int = Intrinsic::aarch64_neon_tbx1;
    s = "vtbx1";
    break;
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
    Int = Intrinsic::aarch64_neon_tbx2;
    s = "vtbx2";
    break;
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
    Int = Intrinsic::aarch64_neon_tbx3;
    s = "vtbx3";
    break;
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    Int = Intrinsic::aarch64_neon_tbx4;
    s = "vtbx4";
    break;
  }
  }

  if (!Int)
    return nullptr;

  // Generic path for vqtbl/vqtbx builtins: map the LLVM intrinsic ID to
  // a CIR intrinsic name string and call directly. These builtins already
  // have 128-bit table operands so no extension is needed (unlike vtbl/vtbx
  // which need 64-to-128 extension).
  StringRef intrincsName;
  switch (Int) {
  case Intrinsic::aarch64_neon_tbl1: intrincsName = "aarch64.neon.tbl1"; break;
  case Intrinsic::aarch64_neon_tbl2: intrincsName = "aarch64.neon.tbl2"; break;
  case Intrinsic::aarch64_neon_tbl3: intrincsName = "aarch64.neon.tbl3"; break;
  case Intrinsic::aarch64_neon_tbl4: intrincsName = "aarch64.neon.tbl4"; break;
  case Intrinsic::aarch64_neon_tbx1: intrincsName = "aarch64.neon.tbx1"; break;
  case Intrinsic::aarch64_neon_tbx2: intrincsName = "aarch64.neon.tbx2"; break;
  case Intrinsic::aarch64_neon_tbx3: intrincsName = "aarch64.neon.tbx3"; break;
  case Intrinsic::aarch64_neon_tbx4: intrincsName = "aarch64.neon.tbx4"; break;
  default:
    CGF.cgm.errorNYI(E->getSourceRange(), "NEON tbl/tbx builtin with intrinsic");
    return {};
  }

  CIRGenBuilderTy &Builder = CGF.getBuilder();
  mlir::Location Loc = CGF.getLoc(E->getExprLoc());
  // Ops already contains the correct operands (the type specifier was
  // excluded by the caller's loop over E->getNumArgs() - 1).
  return emitIntrinsicCallOpCached(CGF.cgm, Builder, Loc, intrincsName, Ty,
                                   mlir::ValueRange(Ops));
}

bool CIRGenFunction::getAArch64SVEProcessedOperands(
    unsigned builtinID, const CallExpr *expr, SmallVectorImpl<mlir::Value> &ops,
    SVETypeFlags typeFlags) {
  // Find out if any arguments are required to be integer constant expressions.
  unsigned iceArguments = 0;
  ASTContext::GetBuiltinTypeError error;
  getContext().GetBuiltinType(builtinID, error, &iceArguments);
  assert(error == ASTContext::GE_None && "Should not codegen an error");

  // Tuple set/get only needs one insert/extract on the aggregate, done by
  // emitSVETupleSetOrGet; do not unpack their struct operand here.
  bool isTupleGetOrSet = typeFlags.isTupleSet() || typeFlags.isTupleGet();

  for (unsigned i = 0, e = expr->getNumArgs(); i != e; i++) {
    bool isIce = iceArguments & (1 << i);

    if (isIce) {
      std::optional<llvm::APSInt> result =
          expr->getArg(i)->getIntegerConstantExpr(getContext());
      assert(result && "Expected argument to be a constant");
      *result = result->extOrTrunc(32);
      ops.push_back(builder.getConstInt(getLoc(expr->getArg(i)->getExprLoc()),
                                        *result));
      continue;
    }

    mlir::Value arg = emitScalarExpr(expr->getArg(i));

    // Multi-vector tuple operands (svintNxM etc.) are lowered to a CIR record
    // of N scalable vectors. Every family except tuple get/set consumes the
    // individual component vectors, so unpack the struct into its members here
    // (mirroring classic GetAArch64SVEProcessedOperands, which emits an
    // extractvalue per struct element). Tuple get/set keep the aggregate whole
    // and index it directly in emitSVETupleSetOrGet.
    if (auto recordTy = dyn_cast<cir::RecordType>(arg.getType());
        recordTy && !isTupleGetOrSet) {
      for (unsigned m = 0, me = recordTy.getMembers().size(); m != me; ++m)
        ops.push_back(cir::ExtractMemberOp::create(
            builder, getLoc(expr->getArg(i)->getExprLoc()), arg, m));
      continue;
    }

    ops.push_back(arg);
  }
  return true;
}

// Forward declarations of the SME lowering helpers (defined after the shared
// SVE predicate-cast / mem-element helpers below, which they reuse).
static mlir::Value emitSVEPredicateCast(CIRGenModule &cgm,
                                        CIRGenBuilderTy &builder,
                                        mlir::Location loc, mlir::Value pred,
                                        cir::VectorType dataVecTy);
static mlir::Value emitSMELd1St1(CIRGenFunction &cgf, const CallExpr *expr,
                                 const SVETypeFlags &typeFlags,
                                 SmallVectorImpl<mlir::Value> &ops,
                                 StringRef intrName);
static mlir::Value emitSMEReadWrite(CIRGenFunction &cgf, const CallExpr *expr,
                                    const SVETypeFlags &typeFlags,
                                    SmallVectorImpl<mlir::Value> &ops,
                                    StringRef intrName);
static mlir::Value emitSMEZero(CIRGenFunction &cgf, const CallExpr *expr,
                               SmallVectorImpl<mlir::Value> &ops,
                               StringRef intrName);
static mlir::Value emitSMELdrStr(CIRGenFunction &cgf, const CallExpr *expr,
                                 SmallVectorImpl<mlir::Value> &ops,
                                 StringRef intrName);
static void swapCommutativeSMEOperands(unsigned builtinID,
                                       SmallVectorImpl<mlir::Value> &ops);

std::optional<mlir::Value>
CIRGenFunction::emitAArch64SMEBuiltinExpr(unsigned builtinID,
                                          const CallExpr *expr) {
  assert(!cir::MissingFeatures::aarch64SMEIntrinsics());

  // Streaming-vector length count builtins are scalar helpers that map to
  // cntsd with a fixed scale factor, matching OGCG lowering.
  auto getCntsScale = [builtinID]() -> unsigned {
    switch (builtinID) {
    default:
      return 0;
    case SME::BI__builtin_sme_svcntsb:
      return 8;
    case SME::BI__builtin_sme_svcntsh:
      return 4;
    case SME::BI__builtin_sme_svcntsw:
      return 2;
    }
  };

  if (unsigned scale = getCntsScale()) {
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Type resTy = convertType(expr->getType());
    mlir::Value cntsd = emitIntrinsicCallOpCached(
        cgm, builder, loc, "aarch64.sme.cntsd", resTy);
    return builder.createMul(loc, cntsd, builder.getUInt64(scale, loc),
                             cir::OverflowBehavior::NoUnsignedWrap |
                                 cir::OverflowBehavior::NoSignedWrap);
  }

  if (builtinID == SME::BI__builtin_sme_svzero_zt) {
    mlir::Location loc = getLoc(expr->getExprLoc());
    auto imm = expr->getArg(0)->getIntegerConstantExpr(getContext());
    assert(imm && "svzero_zt requires a constant immediate");
    return cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("aarch64.sme.zero.zt"),
        builder.getVoidTy(),
        mlir::ValueRange{builder.getConstInt(loc, imm->extOrTrunc(32))})
        .getResult();
  }

  auto *builtinIntrInfo = findARMVectorIntrinsicInMap(
      AArch64SMEIntrinsicMap, builtinID, AArch64SMEIntrinsicsProvenSorted);
  if (builtinIntrInfo && builtinIntrInfo->LLVMIntrinsic != 0) {
    SmallVector<mlir::Value> ops;
    SVETypeFlags typeFlags(builtinIntrInfo->TypeModifier);
    if (!getAArch64SVEProcessedOperands(builtinID, expr, ops, typeFlags))
      return mlir::Value{};

    mlir::Location loc = getLoc(expr->getExprLoc());
    StringRef llvmIntrName =
        Intrinsic::getBaseName(
            static_cast<llvm::Intrinsic::ID>(builtinIntrInfo->LLVMIntrinsic))
            .drop_front(/*strlen("llvm.")=*/5);
    if (llvmIntrName.starts_with("aarch64.sme.readz"))
      return emitIntrinsicCallOpCached(cgm, builder, loc, llvmIntrName,
                                       convertType(expr->getType()),
                                       mlir::ValueRange{ops});

    // ZA tile load/store (svld1_hor/ver, svst1_hor/ver tile-slice). Classic:
    // CodeGenFunction::EmitSMELd1St1.
    if (typeFlags.isLoad() || typeFlags.isStore())
      return emitSMELd1St1(*this, expr, typeFlags, ops, llvmIntrName);

    // ZA tile read/write horizontal+vertical. Classic:
    // CodeGenFunction::EmitSMEReadWrite.
    if (typeFlags.isReadZA() || typeFlags.isWriteZA())
      return emitSMEReadWrite(*this, expr, typeFlags, ops, llvmIntrName);

    // svzero_za / svzero_mask_za. Classic: CodeGenFunction::EmitSMEZero.
    if (builtinID == SME::BI__builtin_sme_svzero_mask_za ||
        builtinID == SME::BI__builtin_sme_svzero_za)
      return emitSMEZero(*this, expr, ops, llvmIntrName);

    // ZA vector load/store. Classic: CodeGenFunction::EmitSMELdrStr.
    if (builtinID == SME::BI__builtin_sme_svldr_vnum_za ||
        builtinID == SME::BI__builtin_sme_svstr_vnum_za ||
        builtinID == SME::BI__builtin_sme_svldr_za ||
        builtinID == SME::BI__builtin_sme_svstr_za)
      return emitSMELdrStr(*this, expr, ops, llvmIntrName);

    // Some SME2 multi-vector forms (svsudot/svsumla) take their two operand
    // groups in the opposite order from the LLVM intrinsic; swap them before
    // the intrinsic call. Mirrors classic swapCommutativeSMEOperands (a no-op
    // for every other builtin). Multi-vector struct operands were already
    // unpacked into their component vectors by getAArch64SVEProcessedOperands.
    swapCommutativeSMEOperands(builtinID, ops);

    // General SME path: outer-product accumulate/subtract (svmopa/svmops), ZA
    // add-horizontal/vertical (svaddha/svaddva), and the SME2 multi-vector
    // (x2/x4) ZA add/sub/dot/mla/read/write families. Mirrors the tail of
    // classic CodeGenFunction::EmitAArch64SMEBuiltinExpr: each svbool_t
    // predicate is narrowed to the data lane count (convert.from.svbool) and
    // the intrinsic is overloaded on the data type (unless OverloadNone).
    //
    // Bail on setsFPMR (the FP8 forms that need an aarch64.set.fpmr call first)
    // so we never emit wrong IR for those; they remain NYI.
    if (!typeFlags.setsFPMR()) {
      cir::VectorType dataTy = getSVEDataType(this, typeFlags);
      for (mlir::Value &op : ops) {
        auto vecTy = dyn_cast<cir::VectorType>(op.getType());
        if (vecTy && vecTy.getIsScalable() &&
            isa<cir::BoolType>(vecTy.getElementType()))
          op = emitSVEPredicateCast(cgm, builder, loc, op, dataTy);
      }

      return emitIntrinsicCallOpCached(cgm, builder, loc, llvmIntrName,
                                       convertType(expr->getType()),
                                       mlir::ValueRange{ops});
    }
  }

  cgm.errorNYI(expr->getSourceRange(),
               std::string("unimplemented AArch64 builtin call: ") +
                   getContext().BuiltinInfo.getName(builtinID));
  return mlir::Value{};
}

// Return the memory element type for an SVE memory-access builtin. Mirrors
// classic CodeGenFunction::SVEBuiltinMemEltTy: only required when it can't be
// inferred from the base-pointer operand (e.g. widening/narrowing ld1/st1).
//
// The chosen integer signedness controls whether a subsequent CIR `integral`
// cast lowers to sext or zext (CIR derives the extension direction from the
// source element sign, whereas classic LLVM uses signless integers plus an
// explicit zext/sext flag). Callers pass `isUnsigned` accordingly.
static mlir::Type getSVEMemEltCIRType(CIRGenBuilderTy &builder,
                                      const SVETypeFlags &typeFlags,
                                      bool isUnsigned) {
  unsigned bits;
  switch (typeFlags.getMemEltType()) {
  case SVETypeFlags::MemEltTyDefault:
    llvm_unreachable("Default MemEltType has no dedicated memory type");
  case SVETypeFlags::MemEltTyInt8:
    bits = 8;
    break;
  case SVETypeFlags::MemEltTyInt16:
    bits = 16;
    break;
  case SVETypeFlags::MemEltTyInt32:
    bits = 32;
    break;
  case SVETypeFlags::MemEltTyInt64:
    bits = 64;
    break;
  default:
    llvm_unreachable("Unknown MemEltType");
  }
  return cir::IntType::get(builder.getContext(), bits, !isUnsigned);
}

// Reinterpret the input predicate so that it can be used to correctly isolate
// the elements of the specified datatype. Mirrors classic
// CodeGenFunction::EmitSVEPredicateCast: svbool_t (<n x 16 x i1>) is converted
// to the narrower predicate class matching the data lane count via
// llvm.aarch64.sve.convert.from.svbool (or .to.svbool when widening back).
static mlir::Value emitSVEPredicateCast(CIRGenModule &cgm,
                                        CIRGenBuilderTy &builder,
                                        mlir::Location loc, mlir::Value pred,
                                        cir::VectorType dataVecTy) {
  cir::VectorType predTy =
      cir::VectorType::get(builder.getBoolTy(), dataVecTy.getSize(),
                           /*isScalable=*/true);
  auto curTy = mlir::cast<cir::VectorType>(pred.getType());
  if (curTy == predTy)
    return pred;

  StringRef intrName = dataVecTy.getSize() == 16
                           ? "aarch64.sve.convert.to.svbool"
                           : "aarch64.sve.convert.from.svbool";
  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, predTy,
                                   mlir::ValueRange{pred});
}

// Memory element type for an SME ZA tile load/store, as a scalable vector with
// the SVE-block lane count (128 / element-bits). Mirrors classic
// getSVEVectorForElementType(SVEBuiltinMemEltTy(TypeFlags)); the resulting
// vector's lane count drives the predicate class (e.g. nxv4i1 for a 32-bit
// element).
static cir::VectorType getSMEMemVecCIRType(CIRGenFunction &cgf,
                                           const SVETypeFlags &typeFlags) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type eltTy;
  switch (typeFlags.getMemEltType()) {
  case SVETypeFlags::MemEltTyDefault:
    eltTy = getSVEDataType(&cgf, typeFlags).getElementType();
    break;
  case SVETypeFlags::MemEltTyInt8:
    eltTy = builder.getSInt8Ty();
    break;
  case SVETypeFlags::MemEltTyInt16:
    eltTy = builder.getSInt16Ty();
    break;
  case SVETypeFlags::MemEltTyInt32:
    eltTy = builder.getSInt32Ty();
    break;
  case SVETypeFlags::MemEltTyInt64:
    eltTy = builder.getSInt64Ty();
    break;
  }
  unsigned bits =
      cgf.getCIRGenModule().getDataLayout().getTypeSizeInBits(eltTy);
  return cir::VectorType::get(eltTy, /*SVEBitsPerBlock=*/128 / bits,
                             /*isScalable=*/true);
}

// Emit an SME ZA tile load/store (svld1_hor/ver, svst1_hor/ver tile-slice).
// Mirrors classic CodeGenFunction::EmitSMELd1St1. ACLE operand order is
// (tile#, slice, pred, ptr [, vnum]); the LLVM intrinsic wants
// (pred, ptr, tile#, slice).
static mlir::Value emitSMELd1St1(CIRGenFunction &cgf, const CallExpr *expr,
                                 const SVETypeFlags &typeFlags,
                                 SmallVectorImpl<mlir::Value> &ops,
                                 StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  cir::VectorType memVecTy = getSMEMemVecCIRType(cgf, typeFlags);
  mlir::Value predicate = emitSVEPredicateCast(cgm, builder, loc, ops[2],
                                               memVecTy);

  mlir::Value basePtr = ops[3];
  mlir::Value realSlice = ops[1];

  // With a vnum parameter, offset the base pointer by vnum streaming-vector
  // lengths (in bytes) and add vnum to the slice index.
  if (ops.size() == 5) {
    mlir::Value cntsd =
        emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.sme.cntsd",
                                  builder.getUInt64Ty());
    mlir::Value svl = builder.createMul(
        loc, cntsd, builder.getUInt64(8, loc),
        cir::OverflowBehavior::NoUnsignedWrap |
            cir::OverflowBehavior::NoSignedWrap);
    // The ACLE vnum is a signed 64-bit scalar; classic multiplies/adds it in
    // signless i64. Match the arithmetic operand types to i64 in CIR.
    mlir::Value vnum = builder.createIntCast(ops[4], builder.getUInt64Ty());
    mlir::Value mulvl =
        builder.createMul(loc, svl, vnum, cir::OverflowBehavior::None);
    // The pointer parameter is void*; stride by bytes to match classic's
    // `getelementptr i8`. Use a plain (no-wrap-free) GEP like classic.
    basePtr = builder.createPtrStride(
        loc, builder.createBitcast(basePtr, builder.getUInt8PtrTy()), mulvl,
        cir::GEPNoWrapFlags::none);
    realSlice = builder.createIntCast(realSlice, builder.getUInt64Ty());
    realSlice =
        builder.createAdd(loc, realSlice, vnum, cir::OverflowBehavior::None);
    realSlice = builder.createIntCast(realSlice, builder.getSInt32Ty());
  }

  SmallVector<mlir::Value, 4> newOps{predicate, basePtr, ops[0], realSlice};
  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{newOps});
}

// Emit an SME ZA tile read/write horizontal+vertical (svread_hor/ver,
// svwrite_hor/ver). Mirrors classic CodeGenFunction::EmitSMEReadWrite: narrow
// the svbool_t predicate to the data lane count and call the intrinsic
// overloaded on the data type.
static mlir::Value emitSMEReadWrite(CIRGenFunction &cgf, const CallExpr *expr,
                                    const SVETypeFlags &typeFlags,
                                    SmallVectorImpl<mlir::Value> &ops,
                                    StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  cir::VectorType dataTy = getSVEDataType(&cgf, typeFlags);
  if (typeFlags.isReadZA())
    ops[1] = emitSVEPredicateCast(cgm, builder, loc, ops[1], dataTy);
  else if (typeFlags.isWriteZA())
    ops[2] = emitSVEPredicateCast(cgm, builder, loc, ops[2], dataTy);

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName,
                                   cgf.convertType(expr->getType()),
                                   mlir::ValueRange{ops});
}

// Emit svzero_za / svzero_mask_za. Mirrors classic
// CodeGenFunction::EmitSMEZero: svzero_za() has no operands and defaults the
// tile mask to 255 (all tiles).
static mlir::Value emitSMEZero(CIRGenFunction &cgf, const CallExpr *expr,
                               SmallVectorImpl<mlir::Value> &ops,
                               StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  if (ops.empty())
    ops.push_back(builder.getSInt32(255, loc));

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{ops});
}

// Some SME2 multi-vector builtins (svsudot/svsumla) name their operand groups
// in the opposite order from the underlying LLVM intrinsic, so the two groups
// (each MultiVec vectors wide, starting at ops[1] after the ZA slice) must be
// swapped before the call. Mirrors classic swapCommutativeSMEOperands exactly.
static void swapCommutativeSMEOperands(unsigned builtinID,
                                       SmallVectorImpl<mlir::Value> &ops) {
  unsigned multiVec;
  switch (builtinID) {
  default:
    return;
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x1:
    multiVec = 1;
    break;
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x2:
  case SME::BI__builtin_sme_svsudot_za32_s8_vg1x2:
    multiVec = 2;
    break;
  case SME::BI__builtin_sme_svsudot_za32_s8_vg1x4:
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x4:
    multiVec = 4;
    break;
  }

  for (unsigned i = 0; i < multiVec; ++i)
    std::swap(ops[i + 1], ops[i + 1 + multiVec]);
}

// SME ZA vector load/store (svldr_za/svstr_za and their _vnum forms).
// Mirrors classic CodeGenFunction::EmitSMELdrStr: the llvm.aarch64.sme.ldr/.str
// intrinsics take (slice i32, ptr, vnum-offset i32). The non-vnum builtins pass
// only (slice, ptr), so append a constant i32 0 offset; the _vnum builtins pass
// the offset as an i64, which is truncated to i32.
static mlir::Value emitSMELdrStr(CIRGenFunction &cgf, const CallExpr *expr,
                                 SmallVectorImpl<mlir::Value> &ops,
                                 StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  if (ops.size() == 2)
    ops.push_back(builder.getConstInt(loc, builder.getSInt32Ty(), 0));
  else
    ops[2] = builder.createIntCast(ops[2], builder.getSInt32Ty());

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{ops});
}

// Emit a contiguous predicated load (svld1 and its widening variants).
// Mirrors classic CodeGenFunction::EmitSVEMaskedLoad.
static mlir::Value emitSVEMaskedLoad(CIRGenFunction &cgf, const CallExpr *expr,
                                     mlir::Type returnTy,
                                     SmallVectorImpl<mlir::Value> &ops,
                                     StringRef intrName, bool isZExtReturn) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  QualType langPtrTy = expr->getArg(1)->getType();
  mlir::Type memEltTy = cgf.convertTypeForMem(
      langPtrTy->castAs<clang::PointerType>()->getPointeeType());

  auto vectorTy = mlir::cast<cir::VectorType>(returnTy);
  cir::VectorType memoryTy = cir::VectorType::get(memEltTy, vectorTy.getSize(),
                                                  /*isScalable=*/true);

  mlir::Value predicate = emitSVEPredicateCast(cgm, builder, loc, ops[0],
                                               memoryTy);
  mlir::Value basePtr = ops[1];

  // Does the load have a vnum offset? ld1 with 3 ACLE operands.
  if (ops.size() > 2)
    basePtr = builder.createPtrStride(loc, basePtr, ops[2]);

  mlir::Value load = emitIntrinsicCallOpCached(
      cgm, builder, loc, intrName, memoryTy,
      mlir::ValueRange{predicate, basePtr});

  if (memoryTy == vectorTy)
    return load;

  // Widening load: extend the loaded narrow elements to the ACLE data type.
  // The pointee (memory) element's signedness selects sext vs zext, matching
  // classic's isZExtReturn behaviour.
  (void)isZExtReturn;
  return builder.createCast(loc, cir::CastKind::integral, load, vectorTy);
}

// Emit a contiguous predicated store (svst1 and its narrowing variants).
// Mirrors classic CodeGenFunction::EmitSVEMaskedStore.
static mlir::Value emitSVEMaskedStore(CIRGenFunction &cgf, const CallExpr *expr,
                                      SmallVectorImpl<mlir::Value> &ops,
                                      StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  QualType langPtrTy = expr->getArg(1)->getType();
  mlir::Type memEltTy = cgf.convertTypeForMem(
      langPtrTy->castAs<clang::PointerType>()->getPointeeType());

  auto vectorTy = mlir::cast<cir::VectorType>(ops.back().getType());
  cir::VectorType memoryTy = cir::VectorType::get(memEltTy, vectorTy.getSize(),
                                                  /*isScalable=*/true);

  mlir::Value predicate = emitSVEPredicateCast(cgm, builder, loc, ops[0],
                                               memoryTy);
  mlir::Value basePtr = ops[1];

  // Does the store have a vnum offset? st1 with 4 ACLE operands.
  if (ops.size() == 4)
    basePtr = builder.createPtrStride(loc, basePtr, ops[2]);

  // Last value is always the data; narrow it for narrowing stores.
  mlir::Value val = ops.back();
  if (memoryTy != vectorTy)
    val = builder.createCast(loc, cir::CastKind::integral, val, memoryTy);

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{val, predicate, basePtr});
}

// Emit an SVE gather load (svld1_gather_*). Mirrors classic
// CodeGenFunction::EmitSVEGatherLoad for the scalar-base/vector-index and
// vector-base/scalar-offset addressing modes.
static mlir::Value emitSVEGatherLoad(CIRGenFunction &cgf, const CallExpr *expr,
                                     mlir::Type returnTy,
                                     SmallVectorImpl<mlir::Value> &ops,
                                     StringRef intrName,
                                     const SVETypeFlags &typeFlags,
                                     bool isZExtReturn) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  auto resultTy = mlir::cast<cir::VectorType>(returnTy);
  mlir::Type memEltTy =
      typeFlags.getMemEltType() == SVETypeFlags::MemEltTyDefault
          ? resultTy.getElementType()
          : getSVEMemEltCIRType(builder, typeFlags, isZExtReturn);
  cir::VectorType overloadedTy = cir::VectorType::get(
      memEltTy, resultTy.getSize(), /*isScalable=*/true);

  bool vectorBase = mlir::isa<cir::VectorType>(ops[1].getType());

  // At the ACLE level there's a single svbool_t predicate; the overloaded
  // intrinsic wants a predicate lane count matching the data.
  ops[0] = emitSVEPredicateCast(cgm, builder, loc, ops[0], overloadedTy);

  // Pass 0 when the offset is missing (only valid for the vector-base mode).
  if (ops.size() == 2)
    ops.push_back(builder.getSInt64(0, loc));

  // For "vector base, scalar index" scale the index to a byte offset.
  if (!typeFlags.isByteIndexed() && vectorBase) {
    unsigned bytesPerElt =
        cgf.getCIRGenModule().getDataLayout().getTypeSizeInBits(memEltTy) / 8;
    ops[2] = builder.createShiftLeft(loc, ops[2], Log2_32(bytesPerElt));
  }

  mlir::Value call = emitIntrinsicCallOpCached(cgm, builder, loc, intrName,
                                               overloadedTy,
                                               mlir::ValueRange{ops});
  if (overloadedTy == resultTy)
    return call;

  // Widening gather: the memory element's signedness selects sext vs zext.
  return builder.createCast(loc, cir::CastKind::integral, call, resultTy);
}

// Emit an SVE scatter store (svst1_scatter_*). Mirrors classic
// CodeGenFunction::EmitSVEScatterStore.
static mlir::Value emitSVEScatterStore(CIRGenFunction &cgf,
                                       const CallExpr *expr,
                                       SmallVectorImpl<mlir::Value> &ops,
                                       StringRef intrName,
                                       const SVETypeFlags &typeFlags) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  auto srcDataTy = mlir::cast<cir::VectorType>(ops.back().getType());
  mlir::Type memEltTy =
      typeFlags.getMemEltType() == SVETypeFlags::MemEltTyDefault
          ? srcDataTy.getElementType()
          : getSVEMemEltCIRType(builder, typeFlags, /*isUnsigned=*/false);
  cir::VectorType overloadedTy = cir::VectorType::get(
      memEltTy, srcDataTy.getSize(), /*isScalable=*/true);

  // In ACLE the source data is the last argument; in LLVM IR it's first.
  mlir::Value data = ops.pop_back_val();
  ops.insert(ops.begin(), data);

  bool vectorBase = mlir::isa<cir::VectorType>(ops[2].getType());

  // Pass 0 when the offset is missing (only valid for the vector-base mode).
  if (ops.size() == 3)
    ops.push_back(builder.getSInt64(0, loc));

  // Truncate the data for narrowing scatters.
  if (overloadedTy != srcDataTy)
    ops[0] = builder.createCast(loc, cir::CastKind::integral, ops[0],
                                overloadedTy);

  ops[1] = emitSVEPredicateCast(cgm, builder, loc, ops[1], overloadedTy);

  // For "vector base, scalar index" scale the index to a byte offset.
  if (!typeFlags.isByteIndexed() && vectorBase) {
    unsigned bytesPerElt =
        cgf.getCIRGenModule().getDataLayout().getTypeSizeInBits(memEltTy) / 8;
    ops[3] = builder.createShiftLeft(loc, ops[3], Log2_32(bytesPerElt));
  }

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{ops});
}

// Emit an SVE multi-vector tuple create (svcreate2/3/4). Mirrors classic
// CodeGenFunction::EmitSVETupleCreate: assemble the N component vectors into a
// poison tuple aggregate via a chain of insertvalue (cir.insert_member).
static mlir::Value emitSVETupleCreate(CIRGenFunction &cgf, const CallExpr *expr,
                                      mlir::Type tupleTy,
                                      SmallVectorImpl<mlir::Value> &ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::Value tuple =
      builder.getConstant(loc, cir::PoisonAttr::get(tupleTy));
  for (unsigned idx = 0, e = ops.size(); idx != e; ++idx)
    tuple = cir::InsertMemberOp::create(builder, loc, tuple, idx, ops[idx]);
  return tuple;
}

// Emit an SVE tuple get/set (svget2/3/4, svset2/3/4). Mirrors classic
// CodeGenFunction::EmitSVETupleSetOrGet: get is an extractvalue, set an
// insertvalue at the constant tuple index.
static mlir::Value emitSVETupleSetOrGet(CIRGenFunction &cgf,
                                        const CallExpr *expr,
                                        const SVETypeFlags &typeFlags,
                                        SmallVectorImpl<mlir::Value> &ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  auto idxConst = ops[1].getDefiningOp<cir::ConstantOp>();
  assert(idxConst && "tuple index must be a constant");
  uint64_t idx =
      mlir::cast<cir::IntAttr>(idxConst.getValue()).getValue().getZExtValue();

  if (typeFlags.isTupleSet())
    return cir::InsertMemberOp::create(builder, loc, ops[0], idx, ops[2]);
  return cir::ExtractMemberOp::create(builder, loc, ops[0], idx);
}

// Emit an SVE structured (multi-vector) load (svld2/3/4 and their _vnum
// variants). Mirrors classic CodeGenFunction::EmitSVEStructLoad: narrow the
// svbool_t predicate to the data lane count, optionally stride the base
// pointer by vnum data vectors, and call the ld{2,3,4}_sret intrinsic which
// returns the tuple aggregate directly.
static mlir::Value emitSVEStructLoad(CIRGenFunction &cgf, const CallExpr *expr,
                                     mlir::Type tupleTy,
                                     const SVETypeFlags &typeFlags,
                                     SmallVectorImpl<mlir::Value> &ops,
                                     StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  cir::VectorType vecTy = getSVEDataType(&cgf, typeFlags);
  mlir::Value predicate = emitSVEPredicateCast(cgm, builder, loc, ops[0], vecTy);
  mlir::Value basePtr = ops[1];

  // The _vnum forms carry a trailing vnum operand: stride the base pointer by
  // vnum data vectors. Classic emits `getelementptr <vscale x N x T>, ptr, idx`
  // (CreateGEP(VTy, BasePtr, Ops[2])), i.e. a GEP whose element type is the SVE
  // data vector, so cast the pointer to point at that vector type first.
  if (ops.size() > 2) {
    auto vecPtrTy = cir::PointerType::get(builder.getContext(), vecTy);
    mlir::Value vecPtr = builder.createBitcast(loc, basePtr, vecPtrTy);
    basePtr = builder.createPtrStride(loc, vecPtr, ops[2],
                                      cir::GEPNoWrapFlags::none);
  }

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, tupleTy,
                                   mlir::ValueRange{predicate, basePtr});
}

// Emit an SVE structured (multi-vector) store (svst2/3/4 and their _vnum
// variants). Mirrors classic CodeGenFunction::EmitSVEStructStore: narrow the
// predicate, optionally stride the base pointer by vnum data vectors, then call
// the st{2,3,4} intrinsic with the component vectors first, then predicate and
// base pointer.
static mlir::Value emitSVEStructStore(CIRGenFunction &cgf, const CallExpr *expr,
                                      const SVETypeFlags &typeFlags,
                                      SmallVectorImpl<mlir::Value> &ops,
                                      StringRef intrName, unsigned n) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  cir::VectorType vecTy = getSVEDataType(&cgf, typeFlags);
  mlir::Value predicate = emitSVEPredicateCast(cgm, builder, loc, ops[0], vecTy);
  mlir::Value basePtr = ops[1];

  // The _vnum forms carry a trailing vnum operand after the tuple; when it is
  // present the operand list has more than (2 + N) entries. Stride by whole SVE
  // data vectors (scalable-vector-typed GEP), matching classic's
  // CreateGEP(VTy, BasePtr, Ops[2]).
  if (ops.size() > (2 + n)) {
    auto vecPtrTy = cir::PointerType::get(builder.getContext(), vecTy);
    mlir::Value vecPtr = builder.createBitcast(loc, basePtr, vecPtrTy);
    basePtr = builder.createPtrStride(loc, vecPtr, ops[2],
                                      cir::GEPNoWrapFlags::none);
  }

  // The tuple has already been unpacked into its N component vectors, which are
  // the last N operands. The intrinsic wants (vec0..vecN-1, predicate, ptr).
  SmallVector<mlir::Value, 6> newOps;
  for (unsigned i = ops.size() - n; i != ops.size(); ++i)
    newOps.push_back(ops[i]);
  newOps.push_back(predicate);
  newOps.push_back(basePtr);

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{newOps});
}

// Emit an SVE widening move (svmovlb/svmovlt). Mirrors classic
// CodeGenFunction::EmitSVEMovl: the widening move is a shift-left-long by an
// immediate 0 (ushllb/ushllt for unsigned, sshllb/sshllt for signed), the
// intrinsic overloaded on the wide result data type (getSVEDataType).
static mlir::Value emitSVEMovl(CIRGenFunction &cgf, const SVETypeFlags &typeFlags,
                               ArrayRef<mlir::Value> ops, StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = ops[0].getLoc();
  cir::VectorType overloadedTy = getSVEDataType(&cgf, typeFlags);
  return emitIntrinsicCallOpCached(
      cgm, builder, loc, intrName, overloadedTy,
      mlir::ValueRange{ops[0], builder.getSInt32(0, loc)});
}

// Emit an SVE2 widening polynomial multiply-long (svpmullb/svpmullt). Mirrors
// classic CodeGenFunction::EmitSVEPMull: splat the scalar operand for the _n
// forms, call the pair-wise intrinsic (pmullb_pair/pmullt_pair) overloaded on
// the NARROWER input data type, then bitcast the result to the wide result data
// type (getSVEDataType). For the 8->16 overload the pair intrinsic is
// pmull{b,t}.pair.nxv16i8 and the reinterpret bitcasts nxv16i8 -> nxv8i16.
static mlir::Value emitSVEPMull(CIRGenFunction &cgf,
                                const SVETypeFlags &typeFlags,
                                SmallVectorImpl<mlir::Value> &ops,
                                StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = ops[0].getLoc();

  // Splat scalar operand to vector (intrinsics with _n infix).
  if (typeFlags.hasSplatOperand()) {
    unsigned opNo = typeFlags.getSplatOperand();
    ops[opNo] = cir::VecSplatOp::create(
        builder, loc, getSVEVectorForElementType(ops[opNo].getType()),
        ops[opNo]);
  }

  // The pair-wise intrinsic is overloaded on the narrower input type.
  auto inputTy = mlir::cast<cir::VectorType>(ops[0].getType());
  mlir::Value call = emitIntrinsicCallOpCached(
      cgm, builder, loc, intrName, inputTy,
      mlir::ValueRange{ops[0], ops[1]});

  // Bitcast to the wider result type.
  cir::VectorType wideTy = getSVEDataType(&cgf, typeFlags);
  return builder.createBitcast(call, wideTy);
}

// Emit a contiguous SVE prefetch (svprfb/svprfh/svprfw/svprfd and their _vnum
// variants). Mirrors classic CodeGenFunction::EmitSVEPrefetchLoad: narrow the
// svbool_t predicate to the memory element lane count, optionally stride the
// base pointer by vnum memory vectors, and call the void aarch64.sve.prf
// intrinsic (overloaded on the predicate type) with {predicate, base, prfop}.
static mlir::Value emitSVEPrefetchLoad(CIRGenFunction &cgf, const CallExpr *expr,
                                       const SVETypeFlags &typeFlags,
                                       SmallVectorImpl<mlir::Value> &ops,
                                       StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  mlir::Type memEltTy =
      getSVEMemEltCIRType(builder, typeFlags, /*isUnsigned=*/false);
  cir::VectorType memoryTy = getSVEVectorForElementType(memEltTy);

  mlir::Value predicate = emitSVEPredicateCast(cgm, builder, loc, ops[0], memoryTy);
  mlir::Value basePtr = ops[1];

  // The _vnum forms carry an index operand between base and prfop. Stride the
  // base pointer by vnum memory vectors (scalable-vector-typed GEP), matching
  // classic's CreateGEP(MemoryTy, BasePtr, Ops[2]).
  if (ops.size() > 3) {
    auto vecPtrTy = cir::PointerType::get(builder.getContext(), memoryTy);
    mlir::Value vecPtr = builder.createBitcast(loc, basePtr, vecPtrTy);
    basePtr = builder.createPtrStride(loc, vecPtr, ops[2],
                                      cir::GEPNoWrapFlags::none);
  }

  mlir::Value prfOp = ops.back();
  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{predicate, basePtr, prfOp});
}

// Emit an SVE gather-prefetch (svprfb/h/w/d_gather_* with a vector of base
// addresses or a scalar base plus vector of indices). Mirrors classic
// CodeGenFunction::EmitSVEGatherPrefetch: the intrinsic is overloaded on the
// vector operand (the bases in "vector base + scalar imm" mode, or the indices
// in "scalar base + vector index" mode); the svbool_t predicate is narrowed to
// that vector's lane count; for the vector-base modes a missing offset is
// passed as 0 with the prfop swapped to the tail, and a present index is scaled
// to a byte offset.
static mlir::Value emitSVEGatherPrefetch(CIRGenFunction &cgf,
                                         const CallExpr *expr,
                                         const SVETypeFlags &typeFlags,
                                         SmallVectorImpl<mlir::Value> &ops,
                                         StringRef intrName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  // The gather prefetches are overloaded on the vector input -- this can either
  // be the vector of base addresses or the vector of offsets.
  auto overloadedTy = mlir::dyn_cast<cir::VectorType>(ops[1].getType());
  if (!overloadedTy)
    overloadedTy = mlir::cast<cir::VectorType>(ops[2].getType());

  // Cast the predicate from svbool_t to the right number of elements.
  ops[0] = emitSVEPredicateCast(cgm, builder, loc, ops[0], overloadedTy);

  // vector + imm addressing modes.
  if (mlir::isa<cir::VectorType>(ops[1].getType())) {
    if (ops.size() == 3) {
      // Pass 0 for 'vector+imm' when the index is omitted.
      ops.push_back(builder.getSInt64(0, loc));
      // The sv_prfop is the last operand in the builtin and IR intrinsic.
      std::swap(ops[2], ops[3]);
    } else {
      // Index needs to be passed as scaled offset.
      mlir::Type memEltTy =
          getSVEMemEltCIRType(builder, typeFlags, /*isUnsigned=*/false);
      unsigned bytesPerElt =
          cgm.getDataLayout().getTypeSizeInBits(memEltTy) / 8;
      if (bytesPerElt > 1)
        ops[2] = builder.createShiftLeft(loc, ops[2], Log2_32(bytesPerElt));
    }
  }

  return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, mlir::Type{},
                                   mlir::ValueRange{ops});
}

// Emit svdupq_n_* (the "duplicate 128-bit quadword" builders). Mirrors classic
// CodeGenFunction::EmitAArch64SVEBuiltinExpr's svdupq handling: materialize the
// N per-lane immediates into a 128-bit fixed vector, insert it into the low
// quadword of a scalable vector, then dupq.lane to broadcast it across every
// quadword. For the predicate forms (svdupq_n_b*) the lane element type is a
// wider integer (128/N bits) and the broadcast quadword is turned into a
// predicate via an all-true ptrue + cmpne(.wide) against zero, then widened to
// svbool_t. The data forms (svdupq_n_{s,u,f}*) stop after dupq.lane.
static mlir::Value emitSVEDupQ(CIRGenFunction &cgf, const CallExpr *expr,
                               mlir::Type ty, const SVETypeFlags &typeFlags,
                               SmallVectorImpl<mlir::Value> &ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  unsigned numOpnds = ops.size();
  auto retTy = mlir::cast<cir::VectorType>(ty);
  bool isBoolTy = mlir::isa<cir::BoolType>(retTy.getElementType());

  // For svdupq_n_b* the element type is an integer of width 128/numelts, so the
  // compare can use the width natural for the expected number of predicate
  // lanes. For the data forms the element type is the operand's own type.
  mlir::Type eltTy = ops[0].getType();
  if (isBoolTy)
    eltTy = cir::IntType::get(builder.getContext(), 128 / numOpnds,
                              /*isSigned=*/false);

  // Zero-extend each operand to the lane element type and build the 128-bit
  // fixed vector. cir.vec.create lowers to the same ConstantVector (all-const)
  // or insertelement-from-poison chain that classic's BuildVector emits. For the
  // data forms the operand already has the lane element type (float or integer),
  // so no cast is emitted (matching classic's no-op CreateZExt(V, V->getType())).
  SmallVector<mlir::Value, 16> vecOps;
  vecOps.reserve(numOpnds);
  for (mlir::Value op : ops) {
    mlir::Value ext = op;
    if (op.getType() != eltTy) {
      // svdupq_n_b* operands are booleans (i1); the rest are already integers.
      ext = mlir::isa<cir::BoolType>(op.getType())
                ? builder.createBoolToInt(op, eltTy)
                : builder.createIntCast(op, eltTy);
    }
    vecOps.push_back(ext);
  }
  auto fixedVecTy = cir::VectorType::get(eltTy, numOpnds, /*isScalable=*/false);
  mlir::Value vec = cir::VecCreateOp::create(builder, loc, fixedVecTy, vecOps);

  // The 128-bit quadword holds exactly numOpnds lanes, so the scalable vector
  // used for dupq.lane has the same lane count (128 / element bits == numOpnds).
  auto overloadedTy =
      cir::VectorType::get(eltTy, numOpnds, /*isScalable=*/true);
  mlir::Value insertSubVec =
      emitAArch64VectorInsert(cgf, loc, overloadedTy, vec);

  mlir::Value dupQLane = emitIntrinsicCallOpCached(
      cgm, builder, loc, "aarch64.sve.dupq.lane", overloadedTy,
      mlir::ValueRange{insertSubVec, builder.getSInt64(0, loc)});

  if (!isBoolTy)
    return dupQLane;

  // Build an all-true governing predicate for the overloaded lane count. Mirrors
  // classic EmitSVEAllTruePred: ptrue with SV_ALL (31).
  auto predTy = cir::VectorType::get(
      builder.getBoolTy(), overloadedTy.getSize(), /*isScalable=*/true);
  mlir::Value pred = emitIntrinsicCallOpCached(
      cgm, builder, loc, "aarch64.sve.ptrue", predTy,
      mlir::ValueRange{builder.getConstInt(loc, builder.getSInt32Ty(),
                                           /*SV_ALL=*/31)});

  // For svdupq_n_b* add a 'cmpne' with '0'. The wide form (numOpnds > 2) takes a
  // <vscale x 2 x i64> zero as its third operand; the numOpnds == 2 (b64) form
  // uses the non-wide cmpne whose third operand is the same nxv2i64 zero.
  auto wideZeroTy = cir::VectorType::get(builder.getSInt64Ty(), 2,
                                         /*isScalable=*/true);
  mlir::Value wideZero = builder.getZero(loc, wideZeroTy);
  StringRef cmpName = numOpnds == 2 ? "aarch64.sve.cmpne"
                                    : "aarch64.sve.cmpne.wide";
  mlir::Value cmp = emitIntrinsicCallOpCached(
      cgm, builder, loc, cmpName, predTy,
      mlir::ValueRange{pred, dupQLane, wideZero});

  // Widen the narrow predicate back to svbool_t (the nxv16i1 result type). For
  // the b8 form the compare already has 16 lanes so this is a no-op; for the
  // narrower forms it emits convert.to.svbool.
  return emitSVEPredicateCast(cgm, builder, loc, cmp, retTy);
}

// Emit svdup_n_b* (broadcast a single bool to a governing predicate).
// Distinct from svdupq_n_b* (the 128-bit quadword builders): svdup_n_b* has a
// single scalar operand. Mirrors classic
// CodeGenFunction::EmitAArch64SVEBuiltinExpr's svdup_n_b handling: compare the
// bool against 0, splat the i1 across a narrow predicate vector
// (getSVEPredType, classic getSVEType), then widen back to svbool_t. The
// cir.vec.splat lowers to the same insertelement + zero-mask shufflevector that
// classic's CreateVectorSplat/EmitSVEDupX emits.
static mlir::Value emitSVEDupB(CIRGenFunction &cgf, const CallExpr *expr,
                               mlir::Type ty, const SVETypeFlags &typeFlags,
                               SmallVectorImpl<mlir::Value> &ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  auto retTy = mlir::cast<cir::VectorType>(ty);

  // cmpne against 0: the operand is a bool (i1), so compare against false.
  mlir::Value cmpNE = builder.createCompare(loc, cir::CmpOpKind::ne, ops[0],
                                            builder.getFalse(loc));

  // Splat the i1 across the narrow predicate vector for the overloaded lane
  // count (classic getSVEType(TypeFlags)).
  cir::VectorType overloadedTy = getSVEPredType(&cgf, typeFlags);
  mlir::Value dup = cir::VecSplatOp::create(builder, loc, overloadedTy, cmpNE);

  // Widen the narrow predicate back to svbool_t. For the b8 form the splat
  // already has 16 lanes so this is a no-op; the narrower forms emit
  // convert.to.svbool.
  return emitSVEPredicateCast(cgm, builder, loc, dup, retTy);
}

// Emit svpsel_lane_{b,c}* (select a predicate from a pair using a lane index
// and a governing predicate). Mirrors classic
// CodeGenFunction::EmitAArch64SVEBuiltinExpr's svpsel_lane handling: the
// llvm.aarch64.sve.psel intrinsic is overloaded on its narrowed second
// predicate operand (getSVEType(TypeFlags), classic), takes an svbool_t
// predicate pair as its first operand and the i32 lane index as its third, and
// returns svbool_t (nxv16i1). For the svcount_t forms (svpsel_lane_c*) the
// predicate pair is passed as svcount_t (!cir.opaque<"aarch64.svcount">), so it
// is converted to svbool_t on the way in (convert.to.svbool) and the result is
// converted back to svcount_t on the way out (convert.from.svbool).
static mlir::Value emitSVEPselLane(CIRGenFunction &cgf, const CallExpr *expr,
                                   const SVETypeFlags &typeFlags,
                                   SmallVectorImpl<mlir::Value> &ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  // The result is svbool_t for the _b* forms and svcount_t for the _c* forms.
  mlir::Type retTy = cgf.convertType(expr->getType());
  bool isSVCount = mlir::isa<cir::OpaqueType>(ops[0].getType());

  // svbool_t governing type (nxv16i1) used both as the psel result and, for the
  // _c* forms, as the intermediate svcount<->svbool round-trip vector type.
  auto svboolTy = cir::VectorType::get(builder.getBoolTy(), /*size=*/16,
                                       /*isScalable=*/true);

  // svcount_t predicate pair -> svbool_t before the intrinsic.
  mlir::Value ops0 = ops[0];
  if (isSVCount)
    ops0 = emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "aarch64.sve.convert.to.svbool", svboolTy,
                                     mlir::ValueRange{ops0});

  // Narrow the second predicate operand to the governing element width
  // (classic getSVEType(TypeFlags)). The lane index (ops[2]) passes through.
  cir::VectorType overloadedTy = getSVEDataType(&cgf, typeFlags);
  ops[1] = emitSVEPredicateCast(cgm, builder, loc, ops[1], overloadedTy);

  mlir::Value psel = emitIntrinsicCallOpCached(
      cgm, builder, loc, "aarch64.sve.psel", svboolTy,
      mlir::ValueRange{ops0, ops[1], ops[2]});

  // svbool_t result -> svcount_t for the _c* forms.
  if (isSVCount)
    return emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "aarch64.sve.convert.from.svbool", retTy,
                                     mlir::ValueRange{psel});
  return psel;
}

std::optional<mlir::Value>
CIRGenFunction::emitAArch64SVEBuiltinExpr(unsigned builtinID,
                                          const CallExpr *expr) {
  if (builtinID >= SVE::BI__builtin_sve_reinterpret_s8_s8 &&
      builtinID <= SVE::BI__builtin_sve_reinterpret_f64_f64_x4) {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AArch64 builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinID));
    return mlir::Value{};
  }

  assert(!cir::MissingFeatures::aarch64SVEIntrinsics());

  auto *builtinIntrInfo = findAArch64SVEBuiltinInMap(
      aarch64SVEIntrinsicMap, builtinID, aarch64SVEIntrinsicsProvenSorted);

  // The operands of the builtin call
  llvm::SmallVector<mlir::Value> ops;

  SVETypeFlags typeFlags(builtinIntrInfo->typeModifier);
  if (!CIRGenFunction::getAArch64SVEProcessedOperands(builtinID, expr, ops,
                                                      typeFlags))
    return mlir::Value{};

  mlir::Location loc = getLoc(expr->getExprLoc());

  // Derive the base LLVM intrinsic name (without the leading "llvm.") once, for
  // the memory-access families that route to dedicated helpers below.
  auto llvmIntrBaseName = [&]() -> std::string {
    std::string name(Intrinsic::getBaseName(
        static_cast<llvm::Intrinsic::ID>(builtinIntrInfo->llvmIntrinsic)));
    name.erase(0, /*std::strlen("llvm.")=*/5);
    return name;
  };

  // Contiguous ld1/st1 match classic exactly at -O0. At -O1+ InstCombine
  // rewrites the ld1/st1 intrinsic to llvm.masked.load/store and classic
  // carries a !tbaa tag onto it (from DecorateInstructionWithTBAA). CIR cannot
  // yet reproduce that tag because cir.call_llvm_intrinsic (and the LLVM
  // dialect llvm.call_intrinsic it lowers to) has no TBAA attribute, so the
  // optimized IR differs only by that alias-analysis hint. The generated code
  // is otherwise identical; wiring TBAA through the intrinsic op is a separate
  // follow-up.
  if (typeFlags.isLoad())
    return emitSVEMaskedLoad(*this, expr, convertType(expr->getType()), ops,
                             llvmIntrBaseName(), typeFlags.isZExtReturn());
  if (typeFlags.isStore())
    return emitSVEMaskedStore(*this, expr, ops, llvmIntrBaseName());
  if (typeFlags.isGatherLoad())
    return emitSVEGatherLoad(*this, expr, convertType(expr->getType()), ops,
                             llvmIntrBaseName(), typeFlags,
                             typeFlags.isZExtReturn());
  if (typeFlags.isScatterStore())
    return emitSVEScatterStore(*this, expr, ops, llvmIntrBaseName(), typeFlags);

  // Multi-vector tuple / structured memory families. Classic:
  // EmitSVEStructLoad/Store, EmitSVETupleSetOrGet/Create, and UndefValue for
  // svundef. The tuple aggregate is a CIR record of N scalable vectors (see
  // CIRGenTypes SVE_VECTOR_TYPE handling), mirroring classic's LLVM StructType.
  if (typeFlags.isStructLoad())
    return emitSVEStructLoad(*this, expr, convertType(expr->getType()),
                             typeFlags, ops, llvmIntrBaseName());
  if (typeFlags.isStructStore()) {
    // Number of data vectors is encoded by the st2/st3/st4 intrinsic.
    unsigned n = mlir::cast<cir::RecordType>(
                     convertType(expr->getArg(expr->getNumArgs() - 1)
                                     ->getType()))
                     .getMembers()
                     .size();
    return emitSVEStructStore(*this, expr, typeFlags, ops, llvmIntrBaseName(),
                              n);
  }
  if (typeFlags.isTupleSet() || typeFlags.isTupleGet())
    return emitSVETupleSetOrGet(*this, expr, typeFlags, ops);
  if (typeFlags.isTupleCreate())
    return emitSVETupleCreate(*this, expr, convertType(expr->getType()), ops);
  if (typeFlags.isUndef())
    return builder.getConstant(
        loc, cir::UndefAttr::get(convertType(expr->getType())));

  // Contiguous prefetch (svprfb/h/w/d + _vnum). Classic: EmitSVEPrefetchLoad.
  if (typeFlags.isPrefetch())
    return emitSVEPrefetchLoad(*this, expr, typeFlags, ops, llvmIntrBaseName());

  // Gather-prefetch (svprfb_gather_* etc.). Classic: EmitSVEGatherPrefetch.
  if (typeFlags.isGatherPrefetch())
    return emitSVEGatherPrefetch(*this, expr, typeFlags, ops,
                                 llvmIntrBaseName());

  // Handle built-ins for which there is a corresponding LLVM Intrinsic.
  // -------------------------------------------------------------------
  if (builtinIntrInfo->llvmIntrinsic != 0) {
    auto retTy = convertType(expr->getType());
    auto intrinsicRetTy = retTy;

    // Emit set FPMR for intrinsics that require it.
    if (typeFlags.setsFPMR())
      emitAArch64SetFPMR(*this, loc, ops.pop_back_val());

    // Predicated intrinsics that take an explicit inactive/passthrough vector
    // as their first operand. For _z the inactive lanes are zeroed; for _x
    // they are undefined. Mirrors classic InsertExplicitZero/UndefOperand.
    if (typeFlags.getMergeType() == SVETypeFlags::MergeZeroExp)
      ops.insert(ops.begin(), builder.getNullValue(retTy, loc));

    if (typeFlags.getMergeType() == SVETypeFlags::MergeAnyExp)
      ops.insert(ops.begin(),
                 builder.getConstant(loc, cir::UndefAttr::get(retTy)));

    // Some ACLE builtins leave out the argument to specify the predicate
    // pattern, which is expected to be expanded to an SV_ALL pattern.
    if (typeFlags.isAppendSVALL())
      ops.push_back(builder.getSInt32(/*SV_ALL=*/31, loc));
    if (typeFlags.isInsertOp1SVALL())
      ops.insert(ops.begin() + 1, builder.getSInt32(/*SV_ALL=*/31, loc));

    // Predicates must match the main datatype.
    //
    // ACLE passes every predicate argument as svbool_t (<vscale x 16 x i1>),
    // but most SVE intrinsics are overloaded on a narrower predicate register
    // class matching the builtin's governing element width (e.g. nxv4i1 for a
    // 32-bit element, nxv2i1 for 64-bit). Mirror classic
    // CodeGenFunction::EmitAArch64SVEBuiltinExpr, which casts EVERY predicate
    // operand to getSVEType(TypeFlags) via EmitSVEPredicateCast — deriving the
    // width from the builtin's TypeFlags element type rather than inferring it
    // from a data operand or the result. That distinction is load-bearing for
    // builtins with no non-predicate scalable operand and no scalable result:
    // svcntp_b{16,32,64} (scalar result — inferring from operands/result would
    // leave the predicates at nxv16i1 and silently emit cntp.nxv16i1, a
    // MISCOMPILE counting every byte lane), and the predicate-only svpnext_b16/
    // 32/64 (which otherwise call pnext with a mismatched nxv16i1 overload). It
    // is also load-bearing for svcvt with mismatched source/destination element
    // widths: the governing predicate follows the wider-element (fewer-lane)
    // operand — getSVEType(TypeFlags) encodes exactly that lane count — whereas
    // inferring from the result overshoots when the result has more lanes than
    // the source (e.g. svcvt_f32_f64 wants nxv2i1, not the result's nxv4i1).
    // emitSVEPredicateCast is a no-op when the predicate is already the right
    // width (e.g. the nxv16i1 svbool_t of the b8 forms).
    cir::VectorType dataTy = getSVEDataType(this, typeFlags);
    for (mlir::Value &op : ops) {
      auto predTy = dyn_cast<cir::VectorType>(op.getType());
      if (!predTy || !predTy.getIsScalable() ||
          !isa<cir::BoolType>(predTy.getElementType()))
        continue;
      op = emitSVEPredicateCast(cgm, builder, loc, op, dataTy);
    }

    if (auto retVecTy = dyn_cast<cir::VectorType>(retTy);
        retVecTy && retVecTy.getIsScalable() &&
        isa<cir::BoolType>(retVecTy.getElementType()))
      intrinsicRetTy = getSVEPredType(this, typeFlags);

    // svunpklo_b/svunpkhi_b lower to llvm.aarch64.sve.punpklo/punpkhi, which
    // are overloaded on their (unchanged svbool_t) operand but return a
    // HALF-width predicate register class (nxv8i1 from nxv16i1). getSVEPredType
    // above gives nxv16i1 (the Bool8 governing type), so halve it to spell the
    // intrinsic's true result; the widening back to svbool_t is handled by the
    // convert.to.svbool below (call type != retTy). Classic gets this for free
    // from the intrinsic's LLVMHalfElementsVectorType return.
    if (builtinID == SVE::BI__builtin_sve_svunpklo_b ||
        builtinID == SVE::BI__builtin_sve_svunpkhi_b)
      intrinsicRetTy = cir::VectorType::get(
          builder.getBoolTy(),
          mlir::cast<cir::VectorType>(intrinsicRetTy).getSize() / 2,
          /*isScalable=*/true);

    // Splat scalar operand to vector (intrinsics with _n infix, plus
    // svdup_lane whose lane index is splatted before the tbl). Mirrors classic
    // EmitSVEDupX: the scalar is Sema-narrowed to the data element's integer
    // width, then splatted to a scalable vector of that lane count. The
    // cir.vec.splat lowers to the same insertelement + zero-mask shufflevector
    // that classic's CreateVectorSplat emits.
    if (typeFlags.hasSplatOperand()) {
      unsigned opNo = typeFlags.getSplatOperand();
      ops[opNo] = cir::VecSplatOp::create(
          builder, loc, getSVEVectorForElementType(ops[opNo].getType()),
          ops[opNo]);
    }

    // Reversed compares (svcmplt/svcmple/svaclt/svacle...) reuse the forward
    // intrinsic (svcmpgt/svcmpge/svacgt/svacge) with the two data operands
    // swapped. Mirrors classic's `std::swap(Ops[1], Ops[2])`.
    if (typeFlags.isReverseCompare())
      std::swap(ops[1], ops[2]);
    if (typeFlags.isReverseUSDOT()) {
      cgm.errorNYI(expr->getSourceRange(),
                   std::string("unimplemented AArch64 builtin call: ") +
                       getContext().BuiltinInfo.getName(builtinID));
      return mlir::Value{};
    }
    // Reversed-operand merge-any binary ops (svsubr/svdivr/... _x): classic
    // folds these into the unpredicated _u intrinsic with the two data operands
    // swapped. Mirrors classic's `std::swap(Ops[1], Ops[2])`.
    if (typeFlags.isReverseMergeAnyBinOp() &&
        typeFlags.getMergeType() == SVETypeFlags::MergeAny)
      std::swap(ops[1], ops[2]);
    // Reversed-operand merge-any accumulate ops (svmad/svmsb/svnmad/svnmsb _x):
    // classic folds these into the unpredicated fmla_u/fmls_u/fnmla_u/fnmls_u
    // (mla_u/mls_u for integer) intrinsic with the multiplicand and addend
    // swapped. Mirrors classic's `std::swap(Ops[1], Ops[3])`.
    if (typeFlags.isReverseMergeAnyAccOp() &&
        typeFlags.getMergeType() == SVETypeFlags::MergeAny)
      std::swap(ops[1], ops[3]);

    // Predicated intrinsics with _z suffix need a select w/ zeroinitializer:
    // the first data operand is zeroed on inactive lanes before the operation.
    if (typeFlags.getMergeType() == SVETypeFlags::MergeZero) {
      mlir::Value splatZero = builder.getNullValue(ops[1].getType(), loc);
      ops[1] = builder.createSelect(loc, ops[0], ops[1], splatZero);
    }

    std::string llvmIntrName(Intrinsic::getBaseName(
        (llvm::Intrinsic::ID)builtinIntrInfo->llvmIntrinsic));

    llvmIntrName.erase(0, /*std::strlen(".llvm")=*/5);

    auto call =
        emitIntrinsicCallOpCached(cgm, builder, loc, llvmIntrName,
                                  intrinsicRetTy, mlir::ValueRange{ops});
    if (call.getType() == retTy)
      return call;

    // Predicate results must be widened back to svbool_t when the overloaded
    // intrinsic returns a narrower predicate register class.
    if (auto retVecTy = dyn_cast<cir::VectorType>(retTy);
        retVecTy && retVecTy.getIsScalable() &&
        isa<cir::BoolType>(retVecTy.getElementType()))
      return emitIntrinsicCallOpCached(cgm, builder, loc,
                                       "aarch64.sve.convert.to.svbool", retTy,
                                       mlir::ValueRange{call});
    // TODO Handle struct types, e.g. svint8x2_t (update the converter first).

    llvm_unreachable("unsupported element count!");
  }

  // Handle the remaining built-ins.
  // -------------------------------
  switch (builtinID) {
  default:
    return std::nullopt;

  case SVE::BI__builtin_sve_svreinterpret_b:
    // svreinterpret_b(svcount_t) -> svbool_t. Classic: convert.to.svbool
    // overloaded on the svcount target-ext type (svcount_t is CIR's
    // !cir.opaque<"aarch64.svcount">, lowering to target("aarch64.svcount")).
    return emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "aarch64.sve.convert.to.svbool",
                                     convertType(expr->getType()),
                                     mlir::ValueRange{ops[0]});

  case SVE::BI__builtin_sve_svreinterpret_c:
    // svreinterpret_c(svbool_t) -> svcount_t. Classic: convert.from.svbool
    // overloaded on (returning) the svcount target-ext type.
    return emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "aarch64.sve.convert.from.svbool",
                                     convertType(expr->getType()),
                                     mlir::ValueRange{ops[0]});

  case SVE::BI__builtin_sve_svpsel_lane_b8:
  case SVE::BI__builtin_sve_svpsel_lane_b16:
  case SVE::BI__builtin_sve_svpsel_lane_b32:
  case SVE::BI__builtin_sve_svpsel_lane_b64:
  case SVE::BI__builtin_sve_svpsel_lane_c8:
  case SVE::BI__builtin_sve_svpsel_lane_c16:
  case SVE::BI__builtin_sve_svpsel_lane_c32:
  case SVE::BI__builtin_sve_svpsel_lane_c64:
    return emitSVEPselLane(*this, expr, typeFlags, ops);

  case SVE::BI__builtin_sve_svmov_b_z: {
    // svmov_b_z(pg, op) <=> svand_b_z(pg, op, op): AND the predicate against
    // itself under the governing predicate. Mirrors classic's svmov_b_z
    // handling, calling llvm.aarch64.sve.and.z overloaded on the Bool8
    // governing type (nxv16i1) -- the exact sibling of svnot_b_z's eor.z.
    cir::VectorType overloadedTy = getSVEDataType(this, typeFlags);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.sve.and.z",
                                     overloadedTy,
                                     mlir::ValueRange{ops[0], ops[1], ops[1]});
  }

  case SVE::BI__builtin_sve_svdup_n_b8:
  case SVE::BI__builtin_sve_svdup_n_b16:
  case SVE::BI__builtin_sve_svdup_n_b32:
  case SVE::BI__builtin_sve_svdup_n_b64:
    return emitSVEDupB(*this, expr, convertType(expr->getType()), typeFlags,
                       ops);

  case SVE::BI__builtin_sve_svdupq_n_b8:
  case SVE::BI__builtin_sve_svdupq_n_b16:
  case SVE::BI__builtin_sve_svdupq_n_b32:
  case SVE::BI__builtin_sve_svdupq_n_b64:
  case SVE::BI__builtin_sve_svdupq_n_u8:
  case SVE::BI__builtin_sve_svdupq_n_s8:
  case SVE::BI__builtin_sve_svdupq_n_u64:
  case SVE::BI__builtin_sve_svdupq_n_f64:
  case SVE::BI__builtin_sve_svdupq_n_s64:
  case SVE::BI__builtin_sve_svdupq_n_u16:
  case SVE::BI__builtin_sve_svdupq_n_f16:
  case SVE::BI__builtin_sve_svdupq_n_bf16:
  case SVE::BI__builtin_sve_svdupq_n_s16:
  case SVE::BI__builtin_sve_svdupq_n_u32:
  case SVE::BI__builtin_sve_svdupq_n_f32:
  case SVE::BI__builtin_sve_svdupq_n_s32:
    return emitSVEDupQ(*this, expr, convertType(expr->getType()), typeFlags,
                       ops);

  case SVE::BI__builtin_sve_svmovlb_u16:
  case SVE::BI__builtin_sve_svmovlb_u32:
  case SVE::BI__builtin_sve_svmovlb_u64:
    return emitSVEMovl(*this, typeFlags, ops, "aarch64.sve.ushllb");

  case SVE::BI__builtin_sve_svmovlb_s16:
  case SVE::BI__builtin_sve_svmovlb_s32:
  case SVE::BI__builtin_sve_svmovlb_s64:
    return emitSVEMovl(*this, typeFlags, ops, "aarch64.sve.sshllb");

  case SVE::BI__builtin_sve_svmovlt_u16:
  case SVE::BI__builtin_sve_svmovlt_u32:
  case SVE::BI__builtin_sve_svmovlt_u64:
    return emitSVEMovl(*this, typeFlags, ops, "aarch64.sve.ushllt");

  case SVE::BI__builtin_sve_svmovlt_s16:
  case SVE::BI__builtin_sve_svmovlt_s32:
  case SVE::BI__builtin_sve_svmovlt_s64:
    return emitSVEMovl(*this, typeFlags, ops, "aarch64.sve.sshllt");

  case SVE::BI__builtin_sve_svpmullt_u16:
  case SVE::BI__builtin_sve_svpmullt_u64:
  case SVE::BI__builtin_sve_svpmullt_n_u16:
  case SVE::BI__builtin_sve_svpmullt_n_u64:
    return emitSVEPMull(*this, typeFlags, ops, "aarch64.sve.pmullt.pair");

  case SVE::BI__builtin_sve_svpmullb_u16:
  case SVE::BI__builtin_sve_svpmullb_u64:
  case SVE::BI__builtin_sve_svpmullb_n_u16:
  case SVE::BI__builtin_sve_svpmullb_n_u64:
    return emitSVEPMull(*this, typeFlags, ops, "aarch64.sve.pmullb.pair");

  case SVE::BI__builtin_sve_svnot_b_z: {
    // svnot_b_z(pg, op) <=> sveor_b_z(pg, op, pg): EOR the predicate against
    // the governing predicate. Mirrors classic's svnot_b_z handling, calling
    // llvm.aarch64.sve.eor.z overloaded on the Bool8 governing type (nxv16i1).
    // The sibling predicate-logic ops (svand_b_z/svorr_b_z/sveor_b_z/...) go
    // through the general intrinsic path; svnot is special only because it
    // duplicates pg as the third operand.
    cir::VectorType overloadedTy = getSVEDataType(this, typeFlags);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.sve.eor.z",
                                     overloadedTy,
                                     mlir::ValueRange{ops[0], ops[1], ops[0]});
  }

  case SVE::BI__builtin_sve_svpfalse_b:
    return builder.getNullValue(convertType(expr->getType()), loc);

  case SVE::BI__builtin_sve_svpfalse_c: {
    mlir::Type svboolTy =
        cir::VectorType::get(builder.getBoolTy(), /*size=*/16,
                             /*isScalable=*/true);
    mlir::Value zeroPred = builder.getNullValue(svboolTy, loc);
    return emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "aarch64.sve.convert.from.svbool",
                                     convertType(expr->getType()),
                                     mlir::ValueRange{zeroPred});
  }

  case SVE::BI__builtin_sve_svlen_u8:
  case SVE::BI__builtin_sve_svlen_s8:
    return genVscaleTimesFactor(loc, cgm, builder, convertType(expr->getType()),
                                16);

  case SVE::BI__builtin_sve_svlen_u16:
  case SVE::BI__builtin_sve_svlen_s16:
  case SVE::BI__builtin_sve_svlen_f16:
  case SVE::BI__builtin_sve_svlen_bf16:
    return genVscaleTimesFactor(loc, cgm, builder, convertType(expr->getType()),
                                8);

  case SVE::BI__builtin_sve_svlen_u32:
  case SVE::BI__builtin_sve_svlen_s32:
  case SVE::BI__builtin_sve_svlen_f32:
    return genVscaleTimesFactor(loc, cgm, builder, convertType(expr->getType()),
                                4);

  case SVE::BI__builtin_sve_svlen_u64:
  case SVE::BI__builtin_sve_svlen_s64:
  case SVE::BI__builtin_sve_svlen_f64:
    return genVscaleTimesFactor(loc, cgm, builder, convertType(expr->getType()),
                                2);

  case SVE::BI__builtin_sve_svtbl2_u8:
  case SVE::BI__builtin_sve_svtbl2_s8:
  case SVE::BI__builtin_sve_svtbl2_u16:
  case SVE::BI__builtin_sve_svtbl2_s16:
  case SVE::BI__builtin_sve_svtbl2_u32:
  case SVE::BI__builtin_sve_svtbl2_s32:
  case SVE::BI__builtin_sve_svtbl2_u64:
  case SVE::BI__builtin_sve_svtbl2_s64:
  case SVE::BI__builtin_sve_svtbl2_f16:
  case SVE::BI__builtin_sve_svtbl2_bf16:
  case SVE::BI__builtin_sve_svtbl2_f32:
  case SVE::BI__builtin_sve_svtbl2_f64: {
    // svtbl2 consumes an x2 tuple (already unpacked into its two component
    // vectors by getAArch64SVEProcessedOperands) plus an index vector, and
    // calls llvm.aarch64.sve.tbl2 overloaded on the data vector type. Mirrors
    // classic EmitAArch64SVEBuiltinExpr's svtbl2 handling.
    cir::VectorType dataTy = getSVEDataType(this, typeFlags);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.sve.tbl2",
                                     dataTy, mlir::ValueRange{ops});
  }

  case SVE::BI__builtin_sve_svset_neonq_s8:
  case SVE::BI__builtin_sve_svset_neonq_s16:
  case SVE::BI__builtin_sve_svset_neonq_s32:
  case SVE::BI__builtin_sve_svset_neonq_s64:
  case SVE::BI__builtin_sve_svset_neonq_u8:
  case SVE::BI__builtin_sve_svset_neonq_u16:
  case SVE::BI__builtin_sve_svset_neonq_u32:
  case SVE::BI__builtin_sve_svset_neonq_u64:
  case SVE::BI__builtin_sve_svset_neonq_f16:
  case SVE::BI__builtin_sve_svset_neonq_f32:
  case SVE::BI__builtin_sve_svset_neonq_f64:
  case SVE::BI__builtin_sve_svset_neonq_bf16:
  case SVE::BI__builtin_sve_svget_neonq_s8:
  case SVE::BI__builtin_sve_svget_neonq_s16:
  case SVE::BI__builtin_sve_svget_neonq_s32:
  case SVE::BI__builtin_sve_svget_neonq_s64:
  case SVE::BI__builtin_sve_svget_neonq_u8:
  case SVE::BI__builtin_sve_svget_neonq_u16:
  case SVE::BI__builtin_sve_svget_neonq_u32:
  case SVE::BI__builtin_sve_svget_neonq_u64:
  case SVE::BI__builtin_sve_svget_neonq_f16:
  case SVE::BI__builtin_sve_svget_neonq_f32:
  case SVE::BI__builtin_sve_svget_neonq_f64:
  case SVE::BI__builtin_sve_svget_neonq_bf16:
  case SVE::BI__builtin_sve_svdup_neonq_s8:
  case SVE::BI__builtin_sve_svdup_neonq_s16:
  case SVE::BI__builtin_sve_svdup_neonq_s32:
  case SVE::BI__builtin_sve_svdup_neonq_s64:
  case SVE::BI__builtin_sve_svdup_neonq_u8:
  case SVE::BI__builtin_sve_svdup_neonq_u16:
  case SVE::BI__builtin_sve_svdup_neonq_u32:
  case SVE::BI__builtin_sve_svdup_neonq_u64:
  case SVE::BI__builtin_sve_svdup_neonq_f16:
  case SVE::BI__builtin_sve_svdup_neonq_f32:
  case SVE::BI__builtin_sve_svdup_neonq_f64:
  case SVE::BI__builtin_sve_svdup_neonq_bf16:
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AArch64 builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinID));
    return {};
  }

  // Unreachable: All cases in the switch above return.
}

/// Emit __builtin_arm_ldrex or __builtin_arm_ldaex for 128-bit types.
/// Maps to @llvm.aarch64.ldxp / @llvm.aarch64.ldaxp intrinsics which return
/// {i64, i64}. The two i64 halves are combined into a single i128 value.
static mlir::Value emitArmLdrex128Intrinsic(unsigned int builtinID,
                                            const CallExpr *clangCallExpr,
                                            CIRGenFunction &cgf) {
  StringRef intrinsicName =
      builtinID == clang::AArch64::BI__builtin_arm_ldrex ? "aarch64.ldxp"
                                                         : "aarch64.ldaxp";

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(clangCallExpr->getExprLoc());

  // Emit the pointer argument.
  mlir::Value ldPtr = cgf.emitScalarExpr(clangCallExpr->getArg(0));

  // The intrinsic returns {i64, i64}. Create an anonymous CIR record type.
  cir::IntType i64Ty = builder.getSInt64Ty();
  cir::RecordType structTy = builder.getAnonRecordTy({i64Ty, i64Ty});

  cir::LLVMIntrinsicCallOp op = cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgf.getCIRGenModule().getStringAttrCached(intrinsicName),
      structTy,
      mlir::ValueRange{ldPtr});
  mlir::Value structVal = op.getResult();

  // Extract values: index 1 = hi, index 0 = lo (matching OGCG ordering).
  mlir::Value valHi =
      cir::ExtractMemberOp::create(builder, loc, structVal, /*index=*/1);
  mlir::Value valLo =
      cir::ExtractMemberOp::create(builder, loc, structVal, /*index=*/0);

  // Zero-extend both to u128 (use unsigned types for zero-extension).
  cir::IntType u64Ty = builder.getUIntNTy(64);
  cir::IntType u128Ty = builder.getUIntNTy(128);
  valHi = builder.createIntCast(valHi, u64Ty);
  valHi = builder.createIntCast(valHi, u128Ty);
  valLo = builder.createIntCast(valLo, u64Ty);
  valLo = builder.createIntCast(valLo, u128Ty);

  // Shift hi left by 64 and OR with lo: result = (hi << 64) | lo.
  mlir::Value shifted = builder.createShiftLeft(loc, valHi, 64u);
  mlir::Value combined = builder.createOr(loc, shifted, valLo);

  // Bitcast to the expected return type (e.g. __int128).
  mlir::Type realResTy = cgf.convertType(clangCallExpr->getType());
  return builder.createBitcast(loc, combined, realResTy);
}

/// Emit __builtin_arm_strex or __builtin_arm_stlex for 128-bit types.
/// Maps to @llvm.aarch64.stxp / @llvm.aarch64.stlxp intrinsics which take
/// two i64 values and a pointer. The 128-bit value is split into two halves
/// by storing to a temp and reloading as {i64, i64}.
static mlir::Value emitArmStrex128Intrinsic(unsigned int builtinID,
                                            const CallExpr *clangCallExpr,
                                            CIRGenFunction &cgf) {
  StringRef intrinsicName =
      builtinID == clang::AArch64::BI__builtin_arm_strex ? "aarch64.stxp"
                                                         : "aarch64.stlxp";

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(clangCallExpr->getExprLoc());

  // Create a temp, emit the 128-bit value into it.
  Address tmp = cgf.createMemTemp(clangCallExpr->getArg(0)->getType(), loc,
                                  ".stxp.tmp");
  cgf.emitAnyExprToMem(clangCallExpr->getArg(0), tmp, Qualifiers(),
                        /*isInitializer=*/true);

  // Reinterpret the temp as {i64, i64} and load it.
  cir::IntType i64Ty = builder.getSInt64Ty();
  cir::RecordType structTy = builder.getAnonRecordTy({i64Ty, i64Ty});
  auto structPtrTy = cir::PointerType::get(builder.getContext(), structTy);
  mlir::Value structPtr =
      builder.createBitcast(loc, tmp.getPointer(), structPtrTy);
  Address structAddr(structPtr, tmp.getAlignment());
  mlir::Value structVal = builder.createLoad(loc, structAddr);

  // Extract the two i64 halves.
  mlir::Value arg0 =
      cir::ExtractMemberOp::create(builder, loc, structVal, /*index=*/0);
  mlir::Value arg1 =
      cir::ExtractMemberOp::create(builder, loc, structVal, /*index=*/1);

  // Emit the pointer to store to.
  mlir::Value stPtr = cgf.emitScalarExpr(clangCallExpr->getArg(1));

  // Call the intrinsic: stxp(i64 lo, i64 hi, ptr addr) -> i32.
  mlir::Type i32Ty = builder.getSInt32Ty();
  llvm::SmallVector<mlir::Value, 3> args = {arg0, arg1, stPtr};
  cir::LLVMIntrinsicCallOp op = cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgf.getCIRGenModule().getStringAttrCached(intrinsicName),
      i32Ty, args);
  return op.getResult();
}

static mlir::Value emitArmLdrexNon128Intrinsic(unsigned int builtinID,
                                               const CallExpr *clangCallExpr,
                                               CIRGenFunction &cgf) {
  StringRef intrinsicName = builtinID == clang::AArch64::BI__builtin_arm_ldrex
                                ? "aarch64.ldxr"
                                : "aarch64.ldaxr";

  // Argument
  mlir::Value loadAddr = cgf.emitScalarExpr(clangCallExpr->getArg(0));
  // Get Instrinc call
  CIRGenBuilderTy &builder = cgf.getBuilder();
  QualType clangResTy = clangCallExpr->getType();
  mlir::Type realResTy = cgf.convertType(clangResTy);
  // Return type of LLVM intrinsic is defined in Intrinsic<arch_type>.td,
  // which can be found under LLVM IR directory.
  mlir::Type funcResTy = builder.getSInt64Ty();
  mlir::Location loc = cgf.getLoc(clangCallExpr->getExprLoc());

  // The LLVM intrinsic requires an elementtype attribute on the pointer
  // argument. Encode the element type width (in bits) as an extra constant
  // operand; the lowering code will strip it and produce the attribute.
  unsigned elemBits = cgf.getContext().getTypeSize(clangResTy);
  mlir::Value elemSizeVal =
      builder.getConstInt(loc, builder.getSInt32Ty(), elemBits);
  llvm::SmallVector<mlir::Value, 2> args = {loadAddr, elemSizeVal};
  cir::LLVMIntrinsicCallOp op = cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgf.getCIRGenModule().getStringAttrCached(intrinsicName),
      funcResTy, args);
  mlir::Value res = op.getResult();

  // Convert result type to the expected type.
  if (mlir::isa<cir::PointerType>(realResTy)) {
    return builder.createIntToPtr(res, realResTy);
  }
  cir::IntType intResTy =
      builder.getSIntNTy(cgf.cgm.getDataLayout().getTypeSizeInBits(realResTy));
  mlir::Value intCastRes = builder.createIntCast(res, intResTy);
  if (mlir::isa<cir::IntType>(realResTy)) {
    return builder.createIntCast(intCastRes, realResTy);
  } else if (mlir::isa<cir::BoolType>(realResTy)) {
    return cir::CastOp::create(builder, loc, realResTy,
                               cir::CastKind::int_to_bool, intCastRes);
  } else if (mlir::isa<cir::SingleType, cir::DoubleType>(realResTy)) {
    // Float/double: truncate i64 to same-width int, then bitcast to float.
    return builder.createBitcast(loc, intCastRes, realResTy);
  } else {
    llvm_unreachable("Unsupported return type for ldrex/ldaex");
  }
}

/// Emit __builtin_arm_strex or __builtin_arm_stlex for non-128-bit types.
/// Maps to @llvm.aarch64.stxr / @llvm.aarch64.stlxr intrinsics.
/// Returns i32: 0 = store succeeded, 1 = store failed (contention).
static mlir::Value emitArmStrexNon128Intrinsic(unsigned int builtinID,
                                               const CallExpr *clangCallExpr,
                                               CIRGenFunction &cgf) {
  StringRef intrinsicName =
      builtinID == clang::AArch64::BI__builtin_arm_strex ? "aarch64.stxr"
                                                         : "aarch64.stlxr";

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(clangCallExpr->getExprLoc());

  // Arg 0: value to store, Arg 1: address.
  mlir::Value storeVal = cgf.emitScalarExpr(clangCallExpr->getArg(0));
  mlir::Value storeAddr = cgf.emitScalarExpr(clangCallExpr->getArg(1));

  // The AArch64 stxr/stlxr intrinsic always takes an i64 value operand.
  // Convert the store value to i64 (matching OGCG CGBuiltin.cpp).
  // OGCG uses ZExtOrBitCast, so use unsigned types for zero-extension.
  cir::IntType u64Ty = builder.getUIntNTy(64);
  if (mlir::isa<cir::PointerType>(storeVal.getType())) {
    storeVal = builder.createPtrToInt(storeVal, u64Ty);
  } else {
    // Cast to unsigned integer of same bit width, then zero-extend to u64.
    // Non-integer types need conversion first.
    if (mlir::isa<cir::BoolType>(storeVal.getType())) {
      storeVal = cir::CastOp::create(builder, loc, builder.getUIntNTy(8),
                                     cir::CastKind::bool_to_int, storeVal);
    } else if (mlir::isa<cir::SingleType, cir::DoubleType>(storeVal.getType())) {
      unsigned valBits =
          cgf.cgm.getDataLayout().getTypeSizeInBits(storeVal.getType());
      storeVal = builder.createBitcast(loc, storeVal, builder.getUIntNTy(valBits));
    }
    unsigned valBits =
        cgf.cgm.getDataLayout().getTypeSizeInBits(storeVal.getType());
    cir::IntType uIntTy = builder.getUIntNTy(valBits);
    storeVal = builder.createIntCast(storeVal, uIntTy);
    storeVal = builder.createIntCast(storeVal, u64Ty);
  }

  // The LLVM intrinsic requires an elementtype attribute on the pointer
  // argument. Encode the element type width (in bits) as an extra constant
  // operand; the lowering code will strip it and produce the attribute.
  QualType argTy = clangCallExpr->getArg(0)->getType();
  unsigned elemBits = cgf.getContext().getTypeSize(argTy);
  mlir::Value elemSizeVal =
      builder.getConstInt(loc, builder.getSInt32Ty(), elemBits);

  // The intrinsic returns i32 (0 = success, 1 = failure).
  mlir::Type i32Ty = builder.getSInt32Ty();
  llvm::SmallVector<mlir::Value, 3> args = {storeVal, storeAddr, elemSizeVal};
  cir::LLVMIntrinsicCallOp op = cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgf.getCIRGenModule().getStringAttrCached(intrinsicName),
      i32Ty, args);
  return op.getResult();
}

/// Given a vector of int type `vecTy`, return a vector type of
/// int type with the same element type width, different signedness,
/// and the same vector size.
static cir::VectorType getSignChangedVectorType(CIRGenBuilderTy &builder,
                                                cir::VectorType vecTy) {
  auto elemTy = mlir::cast<cir::IntType>(vecTy.getElementType());
  elemTy = elemTy.isSigned() ? builder.getUIntNTy(elemTy.getWidth())
                             : builder.getSIntNTy(elemTy.getWidth());
  return cir::VectorType::get(elemTy, vecTy.getSize());
}

static cir::VectorType
getHalfEltSizeTwiceNumElemsVecType(CIRGenBuilderTy &builder,
                                   cir::VectorType vecTy) {
  auto elemTy = mlir::cast<cir::IntType>(vecTy.getElementType());
  elemTy = elemTy.isSigned() ? builder.getSIntNTy(elemTy.getWidth() / 2)
                             : builder.getUIntNTy(elemTy.getWidth() / 2);
  return cir::VectorType::get(elemTy, vecTy.getSize() * 2);
}

static cir::VectorType
castVecOfFPTypeToVecOfIntWithSameWidth(CIRGenBuilderTy &builder,
                                       cir::VectorType vecTy) {
  if (mlir::isa<cir::FP16Type>(vecTy.getElementType()))
    return cir::VectorType::get(builder.getSIntNTy(16), vecTy.getSize());
  if (mlir::isa<cir::SingleType>(vecTy.getElementType()))
    return cir::VectorType::get(builder.getSInt32Ty(), vecTy.getSize());
  if (mlir::isa<cir::DoubleType>(vecTy.getElementType()))
    return cir::VectorType::get(builder.getSInt64Ty(), vecTy.getSize());
  llvm_unreachable(
      "Unsupported element type in getVecOfIntTypeWithSameEltWidth");
}

/// Get integer from a mlir::Value that is an int constant or a constant op.
static int64_t getIntValueFromConstOp(mlir::Value val) {
  return val.getDefiningOp<cir::ConstantOp>().getIntValue().getSExtValue();
}

static mlir::Value emitNeonSplat(CIRGenBuilderTy &builder, mlir::Location loc,
                                 mlir::Value splatVec, mlir::Value splatLane,
                                 unsigned int splatCnt) {
  int64_t splatValInt = getIntValueFromConstOp(splatLane);
  llvm::SmallVector<int64_t, 4> splatMask(splatCnt, splatValInt);
  return builder.createVecShuffle(loc, splatVec, splatMask);
}

/// Build a constant shift amount vector of `vecTy` to shift a vector
/// Here `shitfVal` is a constant integer that will be splated into a
/// a const vector of `vecTy` which is the return of this function
static mlir::Value emitNeonShiftVector(CIRGenBuilderTy &builder,
                                       mlir::Value shiftVal,
                                       cir::VectorType vecTy,
                                       mlir::Location loc, bool neg) {
  int shiftAmt = getIntValueFromConstOp(shiftVal);
  if (neg)
    shiftAmt = -shiftAmt;
  llvm::SmallVector<mlir::Attribute> vecAttr{
      vecTy.getSize(),
      // ConstVectorAttr requires cir::IntAttr
      cir::IntAttr::get(vecTy.getElementType(), shiftAmt)};
  cir::ConstVectorAttr constVecAttr = cir::ConstVectorAttr::get(
      vecTy, mlir::ArrayAttr::get(builder.getContext(), vecAttr));
  return cir::ConstantOp::create(builder, loc, constVecAttr);
}

/// Build ShiftOp of vector type whose shift amount is a vector built
/// from a constant integer using `emitNeonShiftVector` function
static mlir::Value
emitCommonNeonShift(CIRGenBuilderTy &builder, mlir::Location loc,
                    cir::VectorType resTy, mlir::Value shifTgt,
                    mlir::Value shiftAmt, bool shiftLeft, bool negAmt = false) {
  shiftAmt = emitNeonShiftVector(builder, shiftAmt, resTy, loc, negAmt);
  return cir::ShiftOp::create(builder, loc, resTy,
                              builder.createBitcast(shifTgt, resTy), shiftAmt,
                              shiftLeft);
}

/// Right-shift a vector by a constant.
static mlir::Value emitNeonRShiftImm(CIRGenFunction &cgf, mlir::Value shiftVec,
                                     mlir::Value shiftVal,
                                     cir::VectorType vecTy, bool usgn,
                                     mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  int64_t shiftAmt = getIntValueFromConstOp(shiftVal);
  int eltSize =
      cgf.cgm.getDataLayout().getTypeSizeInBits(vecTy.getElementType());

  shiftVec = builder.createBitcast(shiftVec, vecTy);
  // lshr/ashr are undefined when the shift amount is equal to the vector
  // element size.
  if (shiftAmt == eltSize) {
    if (usgn) {
      // Right-shifting an unsigned value by its size yields 0.
      return builder.getZero(loc, vecTy);
    }
    // Right-shifting a signed value by its size is equivalent
    // to a shift of size-1.
    --shiftAmt;
    shiftVal = builder.getConstInt(loc, vecTy.getElementType(), shiftAmt);
  }
  return emitCommonNeonShift(builder, loc, vecTy, shiftVec, shiftVal,
                             false /* right shift */);
}

/// Vectorize value, usually for argument of a neon SISD intrinsic call.
static void vecExtendIntValue(CIRGenFunction &cgf, cir::VectorType argVTy,
                              mlir::Value &arg, mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  cir::IntType eltTy = mlir::dyn_cast<cir::IntType>(argVTy.getElementType());
  assert(mlir::isa<cir::IntType>(arg.getType()) && eltTy);
  // The constant argument to an _n_ intrinsic always has Int32Ty, so truncate
  // it before inserting.
  arg = builder.createIntCast(arg, eltTy);
  mlir::Value zero = builder.getConstInt(loc, cgf.sizeTy, 0);
  mlir::Value poison =
      cir::ConstantOp::create(builder, loc, cir::PoisonAttr::get(eltTy));
  arg = cir::VecInsertOp::create(
      builder, loc, cir::VecSplatOp::create(builder, loc, argVTy, poison), arg,
      zero);
}

/// Reduce vector type value to scalar, usually for result of a
/// neon SISD intrinsic call
static mlir::Value vecReduceIntValue(CIRGenFunction &cgf, mlir::Value val,
                                     mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  assert(mlir::isa<cir::VectorType>(val.getType()));
  return cir::VecExtractOp::create(builder, loc, val,
                                   builder.getConstInt(loc, cgf.sizeTy, 0));
}

template <typename Operation>
static mlir::Value emitNeonCallToOp(
    CIRGenBuilderTy &builder, llvm::SmallVector<mlir::Type> argTypes,
    llvm::SmallVectorImpl<mlir::Value> &args,
    std::optional<llvm::StringRef> intrinsicName, mlir::Type funcResTy,
    mlir::Location loc, bool isConstrainedFPIntrinsic = false,
    unsigned shift = 0, bool rightshift = false,
    CIRGenModule *cgm = nullptr) {
  // TODO: Consider removing the following unreachable when we have
  // constrained FP implemented
  if (isConstrainedFPIntrinsic)
    llvm_unreachable("isConstrainedFPIntrinsic NYI");

  for (unsigned j = 0; j < argTypes.size(); ++j) {
    if (isConstrainedFPIntrinsic) {
    }
    if (shift > 0 && shift == j) {
      args[j] = emitNeonShiftVector(builder, args[j],
                                    mlir::cast<cir::VectorType>(argTypes[j]),
                                    loc, rightshift);
    } else {
      args[j] = builder.createBitcast(args[j], argTypes[j]);
    }
  }
  if (isConstrainedFPIntrinsic) {
    return nullptr;
  }
  if constexpr (std::is_same_v<Operation, cir::LLVMIntrinsicCallOp>) {
    assert(cgm && "AArch64 intrinsic calls must provide CIRGenModule cache");
    mlir::StringAttr intrinsicAttr =
        cgm->getStringAttrCached(intrinsicName.value());
    return Operation::create(builder, loc,
                             intrinsicAttr, funcResTy, args)
        .getResult();
  } else {
    return Operation::create(builder, loc, funcResTy, args).getResult();
  }
}

static mlir::Value emitNeonCall(CIRGenBuilderTy &builder,
                                llvm::SmallVector<mlir::Type> argTypes,
                                llvm::SmallVectorImpl<mlir::Value> &args,
                                llvm::StringRef intrinsicName,
                                mlir::Type funcResTy, mlir::Location loc,
                                bool isConstrainedFPIntrinsic = false,
                                unsigned shift = 0, bool rightshift = false,
                                CIRGenModule *cgm = nullptr) {
  return emitNeonCallToOp<cir::LLVMIntrinsicCallOp>(
      builder, std::move(argTypes), args, intrinsicName, funcResTy, loc,
      isConstrainedFPIntrinsic, shift, rightshift, cgm);
}

static mlir::Value emitNeonCallCached(CIRGenFunction &cgf,
                                      llvm::SmallVector<mlir::Type> argTypes,
                                      llvm::SmallVectorImpl<mlir::Value> &args,
                                      llvm::StringRef intrinsicName,
                                      mlir::Type funcResTy,
                                      mlir::Location loc) {
  return emitNeonCall(cgf.getBuilder(), std::move(argTypes), args,
                      intrinsicName, funcResTy, loc, false, 0, false,
                      &cgf.getCIRGenModule());
}

static mlir::Value emitNeonCallCached(CIRGenFunction &cgf,
                                      llvm::SmallVector<mlir::Type> argTypes,
                                      llvm::SmallVectorImpl<mlir::Value> &args,
                                      llvm::StringRef intrinsicName,
                                      mlir::Type funcResTy, mlir::Location loc,
                                      bool isConstrainedFPIntrinsic,
                                      unsigned shift, bool rightshift) {
  return emitNeonCall(cgf.getBuilder(), std::move(argTypes), args,
                      intrinsicName, funcResTy, loc,
                      isConstrainedFPIntrinsic, shift, rightshift,
                      &cgf.getCIRGenModule());
}

/// This function `emitCommonNeonCallPattern0` implements a common way
///  to generate neon intrinsic call that has following pattern:
///  1. There is a need to cast result of the intrinsic call back to
///     expression type.
///  2. Function arg types are given, not deduced from actual arg types.
static mlir::Value
emitCommonNeonCallPattern0(CIRGenFunction &cgf, llvm::StringRef intrincsName,
                           llvm::SmallVector<mlir::Type> argTypes,
                           llvm::SmallVectorImpl<mlir::Value> &ops,
                           mlir::Type funcResTy, const clang::CallExpr *e) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  if (argTypes.empty()) {
    // The most common arg types is {funcResTy, funcResTy} for neon intrinsic
    // functions. Thus, it is as default so call site does not need to
    // provide it. Every neon intrinsic function has at least one argument,
    // Thus empty argTypes really just means {funcResTy, funcResTy}.
    argTypes = {funcResTy, funcResTy};
  }
  mlir::Value res = emitNeonCallCached(cgf, std::move(argTypes), ops,
                                       intrincsName, funcResTy,
                                       cgf.getLoc(e->getExprLoc()));
  mlir::Type resultType = cgf.convertType(e->getType());
  return builder.createBitcast(res, resultType);
}

/// The function `emitCommonNeonVecAcrossCall` implements a common way
/// to implement neon intrinsic which has the following pattern:
///  1. There is only one argument which is of vector type
///  2. The result of the neon intrinsic is the element type of the input.
/// This type of intrinsic usually is for across operations of the input vector.

static mlir::Value emitCommonNeonVecAcrossCall(CIRGenFunction &cgf,
                                               llvm::StringRef intrincsName,
                                               mlir::Type eltTy,
                                               unsigned vecLen,
                                               const clang::CallExpr *e) {
  mlir::Value op = cgf.emitScalarExpr(e->getArg(0));
  cir::VectorType vTy = cir::VectorType::get(eltTy, vecLen);
  llvm::SmallVector<mlir::Value, 1> args{op};
  return emitNeonCallCached(cgf, {vTy}, args, intrincsName, eltTy,
                            cgf.getLoc(e->getExprLoc()));
}

mlir::Value CIRGenFunction::emitCommonNeonBuiltinExpr(
    unsigned builtinID, unsigned llvmIntrinsic, unsigned altLLVMIntrinsic,
    const char *nameHint, unsigned modifier, const CallExpr *e,
    llvm::SmallVectorImpl<mlir::Value> &ops, Address ptrOp0, Address ptrOp1,
    llvm::Triple::ArchType arch) {
  // Get the last argument, which specifies the vector type.
  const clang::Expr *arg = e->getArg(e->getNumArgs() - 1);
  std::optional<llvm::APSInt> neonTypeConst =
      arg->getIntegerConstantExpr(getContext());
  if (!neonTypeConst)
    return nullptr;

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags neonType(neonTypeConst->getZExtValue());
  bool isUnsigned = neonType.isUnsigned();
  bool isQuad = neonType.isQuad();
  const bool hasLegalHalfType = getTarget().hasFastHalfType();
  // The value of allowBFloatArgsAndRet is true for AArch64.
  // TODO: This should come from ABI info when the method is available.
  const bool allowBFloatArgsAndRet = true;

  cir::VectorType vTy = GetNeonType(this, neonType, hasLegalHalfType, false,
                                    allowBFloatArgsAndRet);
  mlir::Type ty = vTy;
  if (!ty)
    return nullptr;

  unsigned intrinicId = llvmIntrinsic;
  if ((modifier & UnsignedAlts) && !isUnsigned)
    intrinicId = altLLVMIntrinsic;

  // This first switch is for the intrinsics that cannot have a more generic
  // codegen solution.
  switch (builtinID) {
  default:
    break;
  case NEON::BI__builtin_neon_splat_lane_v:
  case NEON::BI__builtin_neon_splat_laneq_v:
  case NEON::BI__builtin_neon_splatq_lane_v:
  case NEON::BI__builtin_neon_splatq_laneq_v: {
    uint64_t numElements = vTy.getSize();
    if (builtinID == NEON::BI__builtin_neon_splatq_lane_v)
      numElements = numElements << 1;
    if (builtinID == NEON::BI__builtin_neon_splat_laneq_v)
      numElements = numElements >> 1;
    ops[0] = builder.createBitcast(ops[0], vTy);
    return emitNeonSplat(builder, getLoc(e->getExprLoc()), ops[0], ops[1],
                         numElements);
  }
  case NEON::BI__builtin_neon_vabs_v:
  case NEON::BI__builtin_neon_vabsq_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    ops[0] = builder.createBitcast(ops[0], vTy);
    if (mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(
            vTy.getElementType())) {
      return cir::FAbsOp::create(builder, loc, ops[0]);
    }
    // Integer abs: use llvm.aarch64.neon.abs intrinsic matching OGCG.
    return emitNeonCallCached(*this, {vTy}, ops, "aarch64.neon.abs", vTy, loc);
  }
  case NEON::BI__builtin_neon_vadd_v:
  case NEON::BI__builtin_neon_vaddq_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    auto byteVecTy = cir::VectorType::get(sInt8Ty, isQuad ? 16 : 8);
    ops[0] = builder.createBitcast(ops[0], byteVecTy);
    ops[1] = builder.createBitcast(ops[1], byteVecTy);
    mlir::Value xorVal =
        builder.createBinop(loc, ops[0], cir::BinOpKind::Xor, ops[1]);
    return xorVal.getType() == ty ? xorVal : builder.createBitcast(xorVal, ty);
  }
  case NEON::BI__builtin_neon_vaddhn_v: {
    // vaddhn_v: Add and narrow high - adds two wide vectors and returns
    // narrow result with high bits (add vectors, shift right, truncate)
    mlir::Location loc = getLoc(e->getExprLoc());
    bool isSigned = mlir::cast<cir::IntType>(vTy.getElementType()).isSigned();

    // Get extended element vector type (e.g., <8 x i16> from <8 x i8> result)
    cir::VectorType srcTy =
        builder.getExtendedOrTruncatedElementVectorType(vTy, true, isSigned);

    // Bitcast operands to extended type
    ops[0] = builder.createBitcast(ops[0], srcTy);
    ops[1] = builder.createBitcast(ops[1], srcTy);

    // Add the vectors: %sum = add <N x iWIDE> %lhs, %rhs
    ops[0] = builder.createBinop(loc, ops[0], cir::BinOpKind::Add, ops[1]);

    // Shift right by half element width: %high = lshr <N x iWIDE> %sum, WIDE/2
    auto srcElemTy = mlir::cast<cir::IntType>(srcTy.getElementType());
    unsigned shiftAmt = srcElemTy.getWidth() / 2;
    mlir::Value shiftVal = builder.getConstInt(loc, srcElemTy, shiftAmt);
    mlir::Value shiftVec =
        emitNeonShiftVector(builder, shiftVal, srcTy, loc, false);
    ops[0] = builder.createLShr(loc, ops[0], shiftVec);

    // Truncate to result type: %res = trunc <N x iWIDE> %high to <N x iNARROW>
    return builder.createIntCast(ops[0], vTy);
  }
  case NEON::BI__builtin_neon_vsubhn_v: {
    // vsubhn_v: Subtract and narrow high - subtracts two wide vectors and
    // returns narrow result with high bits.
    mlir::Location loc = getLoc(e->getExprLoc());
    bool isSigned = mlir::cast<cir::IntType>(vTy.getElementType()).isSigned();

    // Get extended element vector type (e.g., <8 x i16> from <8 x i8> result)
    cir::VectorType srcTy =
        builder.getExtendedOrTruncatedElementVectorType(vTy, true, isSigned);

    // Bitcast operands to extended type.
    ops[0] = builder.createBitcast(ops[0], srcTy);
    ops[1] = builder.createBitcast(ops[1], srcTy);

    // Subtract the vectors.
    ops[0] = builder.createBinop(loc, ops[0], cir::BinOpKind::Sub, ops[1]);

    // Shift right by half element width.
    auto srcElemTy = mlir::cast<cir::IntType>(srcTy.getElementType());
    unsigned shiftAmt = srcElemTy.getWidth() / 2;
    mlir::Value shiftVal = builder.getConstInt(loc, srcElemTy, shiftAmt);
    mlir::Value shiftVec =
        emitNeonShiftVector(builder, shiftVal, srcTy, loc, false);
    ops[0] = builder.createLShr(loc, ops[0], shiftVec);

    // Truncate to result type.
    return builder.createIntCast(ops[0], vTy);
  }
  case NEON::BI__builtin_neon_vclz_v:
  case NEON::BI__builtin_neon_vclzq_v: {
    // ctlz takes a second i1 argument indicating whether clz(0) is poison.
    mlir::Location loc = getLoc(e->getExprLoc());
    ops[0] = builder.createBitcast(ops[0], vTy);
    mlir::Value isZeroPoison =
        getTarget().isCLZForZeroUndef() ? builder.getTrue(loc)
                                        : builder.getFalse(loc);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "ctlz", vTy,
                                     mlir::ValueRange{ops[0], isZeroPoison});
  }
  case NEON::BI__builtin_neon_vcnt_v:
  case NEON::BI__builtin_neon_vcntq_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    ops[0] = builder.createBitcast(ops[0], vTy);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "ctpop", vTy,
                                     mlir::ValueRange{ops[0]});
  }
  case NEON::BI__builtin_neon_vcvt_f16_s16:
  case NEON::BI__builtin_neon_vcvt_f16_u16:
  case NEON::BI__builtin_neon_vcvtq_f16_s16:
  case NEON::BI__builtin_neon_vcvtq_f16_u16: {
    ops[0] = builder.createBitcast(ops[0], ty);
    mlir::Type floatTy =
        cir::VectorType::get(cir::FP16Type::get(builder.getContext()),
                             vTy.getSize());
    return builder.createCast(cir::CastKind::int_to_float, ops[0], floatTy);
  }
  case NEON::BI__builtin_neon_vmovl_v: {
    cir::VectorType dTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /* truncate */,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    // This cast makes sure arg type conforms intrinsic expected arg type.
    ops[0] = builder.createBitcast(ops[0], dTy);
    return builder.createIntCast(ops[0], ty);
  }
  case NEON::BI__builtin_neon_vmovn_v: {
    cir::VectorType qTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true, mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    ops[0] = builder.createBitcast(ops[0], qTy);
    // It really is truncation in this context.
    // In CIR, integral cast op supports vector of int type truncating.
    return builder.createIntCast(ops[0], ty);
  }
  case NEON::BI__builtin_neon_vpaddl_v:
  case NEON::BI__builtin_neon_vpaddlq_v: {
    // The source operand type has twice as many elements of half the size.
    cir::VectorType narrowTy = getHalfEltSizeTwiceNumElemsVecType(builder, vTy);
    return emitNeonCallCached(*this, {narrowTy}, ops,
                              isUnsigned ? "aarch64.neon.uaddlp"
                                         : "aarch64.neon.saddlp",
                              vTy, getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vqdmlal_v:
  case NEON::BI__builtin_neon_vqdmlsl_v: {
    llvm::SmallVector<mlir::Value, 2> mulOps(ops.begin() + 1, ops.end());
    cir::VectorType srcVty = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false, /* truncate */
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    ops[1] = emitNeonCallCached(*this, {srcVty, srcVty}, mulOps,
                                "aarch64.neon.sqdmull", vTy,
                                getLoc(e->getExprLoc()));
    ops.resize(2);
    return emitNeonCallCached(*this, {vTy, vTy}, ops,
                              builtinID == NEON::BI__builtin_neon_vqdmlal_v
                                  ? "aarch64.neon.sqadd"
                                  : "aarch64.neon.sqsub",
                              vTy, getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvt_f32_v:
  case NEON::BI__builtin_neon_vcvtq_f32_v: {
    ops[0] = builder.createBitcast(ops[0], ty);
    ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float32, false, isQuad),
                     hasLegalHalfType);
    return builder.createCast(cir::CastKind::int_to_float, ops[0], ty);
  }
  case NEON::BI__builtin_neon_vext_v:
  case NEON::BI__builtin_neon_vextq_v: {
    int cv = getIntValueFromConstOp(ops[2]);
    llvm::SmallVector<int64_t, 16> indices;
    for (unsigned i = 0, e = vTy.getSize(); i != e; ++i)
      indices.push_back(i + cv);

    ops[0] = builder.createBitcast(ops[0], ty);
    ops[1] = builder.createBitcast(ops[1], ty);
    return builder.createVecShuffle(getLoc(e->getExprLoc()), ops[0], ops[1],
                                    indices);
  }
  case NEON::BI__builtin_neon_vqdmulhq_lane_v:
  case NEON::BI__builtin_neon_vqdmulh_lane_v:
  case NEON::BI__builtin_neon_vqrdmulhq_lane_v:
  case NEON::BI__builtin_neon_vqrdmulh_lane_v: {
    cir::VectorType resTy =
        (builtinID == NEON::BI__builtin_neon_vqdmulhq_lane_v ||
         builtinID == NEON::BI__builtin_neon_vqrdmulhq_lane_v)
            ? cir::VectorType::get(vTy.getElementType(), vTy.getSize() * 2)
            : vTy;
    cir::VectorType mulVecT =
        GetNeonType(this, NeonTypeFlags(neonType.getEltType(), false,
                                        /*isQuad*/ false));
    return emitNeonCallCached(
        *this, {resTy, mulVecT, sInt32Ty}, ops,
        (builtinID == NEON::BI__builtin_neon_vqdmulhq_lane_v ||
         builtinID == NEON::BI__builtin_neon_vqdmulh_lane_v)
            ? "aarch64.neon.sqdmulh.lane"
            : "aarch64.neon.sqrdmulh.lane",
        resTy, getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vqdmulhq_laneq_v:
  case NEON::BI__builtin_neon_vqdmulh_laneq_v:
  case NEON::BI__builtin_neon_vqrdmulhq_laneq_v:
  case NEON::BI__builtin_neon_vqrdmulh_laneq_v: {
    cir::VectorType mulVecT =
        GetNeonType(this, NeonTypeFlags(neonType.getEltType(), false,
                                        /*isQuad*/ true));
    return emitNeonCallCached(
        *this, {vTy, mulVecT, sInt32Ty}, ops,
        (builtinID == NEON::BI__builtin_neon_vqdmulhq_laneq_v ||
         builtinID == NEON::BI__builtin_neon_vqdmulh_laneq_v)
            ? "aarch64.neon.sqdmulh.laneq"
            : "aarch64.neon.sqrdmulh.laneq",
        vTy, getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vqshl_n_v:
  case NEON::BI__builtin_neon_vqshlq_n_v: {
    // Saturating shift left by immediate. The shift amount (second arg) is a
    // scalar that needs splatting into a vector.
    return emitNeonCallCached(
        *this, {vTy, vTy}, ops,
        isUnsigned ? "aarch64.neon.uqshl" : "aarch64.neon.sqshl", vTy,
        getLoc(e->getExprLoc()), false, /* not fp constrained op */
        1,                              /* second arg is shift amount */
        false /* leftshift */);
  }
  case NEON::BI__builtin_neon_vqshlu_n_v:
  case NEON::BI__builtin_neon_vqshluq_n_v: {
    // These intrinsics expect signed vector type as input, but
    // return unsigned vector type.
    cir::VectorType srcTy = getSignChangedVectorType(builder, vTy);
    return emitNeonCallCached(*this, {srcTy, srcTy}, ops,
                              "aarch64.neon.sqshlu", vTy,
                              getLoc(e->getExprLoc()),
                              false, /* not fp constrained op */
                              1,     /* second arg is shift amount */
                              false /* leftshift */);
  }
  case NEON::BI__builtin_neon_vrshr_n_v:
  case NEON::BI__builtin_neon_vrshrq_n_v: {
    return emitNeonCallCached(
        *this,
        {vTy, isUnsigned ? getSignChangedVectorType(builder, vTy) : vTy}, ops,
        isUnsigned ? "aarch64.neon.urshl" : "aarch64.neon.srshl", vTy,
        getLoc(e->getExprLoc()), false, /* not fp constrained op*/
        1,                              /* second arg is shift amount */
        true /* rightshift */);
  }
  case NEON::BI__builtin_neon_vshl_n_v:
  case NEON::BI__builtin_neon_vshlq_n_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    return emitCommonNeonShift(builder, loc, vTy, ops[0], ops[1], true);
  }
  case NEON::BI__builtin_neon_vshll_n_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    cir::VectorType srcTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /* truncate */,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    ops[0] = builder.createBitcast(ops[0], srcTy);
    // The following cast will be lowered to SExt or ZExt in LLVM.
    ops[0] = builder.createIntCast(ops[0], vTy);
    return emitCommonNeonShift(builder, loc, vTy, ops[0], ops[1], true);
  }
  case NEON::BI__builtin_neon_vshrn_n_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    cir::VectorType srcTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /* extended */,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    ops[0] = builder.createBitcast(ops[0], srcTy);
    ops[0] = emitCommonNeonShift(builder, loc, srcTy, ops[0], ops[1], false);
    return builder.createIntCast(ops[0], vTy);
  }
  case NEON::BI__builtin_neon_vshr_n_v:
  case NEON::BI__builtin_neon_vshrq_n_v:
    return emitNeonRShiftImm(*this, ops[0], ops[1], vTy, isUnsigned,
                             getLoc(e->getExprLoc()));
  case NEON::BI__builtin_neon_vtst_v:
  case NEON::BI__builtin_neon_vtstq_v: {
    mlir::Location loc = getLoc(e->getExprLoc());
    ops[0] = builder.createBitcast(ops[0], ty);
    ops[1] = builder.createBitcast(ops[1], ty);
    ops[0] = builder.createAnd(loc, ops[0], ops[1]);
    // Note that during vmVM Lowering, result of `VecCmpOp` is sign extended,
    // matching traditional codegen behavior.
    return cir::VecCmpOp::create(builder, loc, ty, cir::CmpOpKind::ne, ops[0],
                                 builder.getZero(loc, ty));
  }
  }

  // This second switch is for the intrinsics that might have a more generic
  // codegen solution so we can use the common codegen in future.
  llvm::StringRef intrincsName;
  llvm::SmallVector<mlir::Type> argTypes;
  switch (builtinID) {
  default: {
    // For intrinsics not explicitly handled above, use the intrinsic map info
    // to emit a generic intrinsic call. Only safe for aarch64.neon.*
    // intrinsics where all args AND result share the same type (or where
    // Add1ArgType provides the argument type for narrowing/widening ops).
    // Standard LLVM intrinsics (ctlz, fabs, etc.) have different calling
    // conventions and must be handled explicitly.
    unsigned typeMods = modifier & ~(UnsignedAlts | Add1ArgType);
    if (intrinicId && !typeMods) {
      llvm::StringRef fullName = llvm::Intrinsic::getBaseName(
          static_cast<llvm::Intrinsic::ID>(intrinicId));
      llvm::StringRef stripped = fullName.drop_front(5); // drop "llvm."
      // Only handle aarch64.neon.* intrinsics generically.
      if (stripped.starts_with("aarch64.neon.")) {
        intrincsName = stripped;
        if ((modifier & Add1ArgType) &&
            mlir::isa<cir::IntType>(vTy.getElementType()) &&
            !ops.empty() && [&]() {
              // Compare total bit widths to detect narrowing intrinsics.
              // Narrowing ops have wider inputs (e.g. 128-bit <16 x i8>)
              // than the result (e.g. 64-bit <8 x i8>). Non-narrowing ops
              // like pmul and vqshl have same-width inputs and results.
              auto opVecTy =
                  mlir::cast<cir::VectorType>(ops[0].getType());
              auto opElemTy =
                  mlir::cast<cir::IntType>(opVecTy.getElementType());
              auto resElemTy =
                  mlir::cast<cir::IntType>(vTy.getElementType());
              unsigned opBits = opVecTy.getSize() * opElemTy.getWidth();
              unsigned resBits = vTy.getSize() * resElemTy.getWidth();
              return opBits > resBits;
            }()) {
          // Integer narrowing intrinsics: the ops are byte-vectors of the
          // wider input (e.g. <16 x i8> from int16x8_t with 128 total bits)
          // while the result is narrower (e.g. <8 x i8> with 64 total bits).
          // Compute the wider arg type (e.g. <8 x i16> from <8 x i8> for
          // raddhn). Non-narrowing intrinsics with Add1ArgType (e.g. pmul,
          // vqshl) have same total bit width and use the default path.
          bool isSigned =
              mlir::cast<cir::IntType>(vTy.getElementType()).isSigned();
          mlir::Type argTy =
              builder.getExtendedOrTruncatedElementVectorType(vTy, true,
                                                              isSigned);
          for (size_t i = 0; i < ops.size(); ++i)
            argTypes.push_back(argTy);
        } else {
          for (size_t i = 0; i < ops.size(); ++i)
            argTypes.push_back(vTy);
        }
        break;
      }
    }
    // Return nullptr so the caller tries the AArch64-specific switch.
    return nullptr;
  }
  case NEON::BI__builtin_neon_vbfdot_f32:
  case NEON::BI__builtin_neon_vbfdotq_f32:
  case NEON::BI__builtin_neon_vbfmmlaq_f32:
  case NEON::BI__builtin_neon_vbfmlalbq_f32:
  case NEON::BI__builtin_neon_vbfmlaltq_f32: {
    // BFloat16 dot product / matrix-multiply / multiply-long: the
    // accumulator/result is a float vector while the two inputs are bfloat16
    // vectors with twice the element count (same total bit width). These differ
    // only by the underlying intrinsic; all have the signature
    //   (<N x float>, <2N x bfloat>, <2N x bfloat>) -> <N x float>.
    // The inputs MUST be given bfloat16 argTypes here: otherwise the generic
    // NEONMAP path bitcasts them to the float result type and the intrinsic
    // call fails CIR verification ("operand #1 has type <4 x float> but
    // 'llvm.aarch64.neon.bfmmla' expects <8 x bfloat>").
    switch (builtinID) {
    case NEON::BI__builtin_neon_vbfdot_f32:
    case NEON::BI__builtin_neon_vbfdotq_f32:
      intrincsName = "aarch64.neon.bfdot";
      break;
    case NEON::BI__builtin_neon_vbfmmlaq_f32:
      intrincsName = "aarch64.neon.bfmmla";
      break;
    case NEON::BI__builtin_neon_vbfmlalbq_f32:
      intrincsName = "aarch64.neon.bfmlalb";
      break;
    case NEON::BI__builtin_neon_vbfmlaltq_f32:
      intrincsName = "aarch64.neon.bfmlalt";
      break;
    default:
      llvm_unreachable("unexpected bf16 builtin");
    }
    auto bf16Ty = cir::BF16Type::get(builder.getContext());
    auto inputTy = cir::VectorType::get(bf16Ty, vTy.getSize() * 2);
    argTypes.push_back(vTy);     // accumulator: <N x float>
    argTypes.push_back(inputTy); // input1: <2N x bfloat16>
    argTypes.push_back(inputTy); // input2: <2N x bfloat16>
    break;
  }
  case NEON::BI__builtin_neon_vcale_v:
  case NEON::BI__builtin_neon_vcaleq_v:
  case NEON::BI__builtin_neon_vcalt_v:
  case NEON::BI__builtin_neon_vcaltq_v:
    // vcale/vcalt = vcage/vcagt with swapped operands
    std::swap(ops[0], ops[1]);
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcage_v:
  case NEON::BI__builtin_neon_vcageq_v:
  case NEON::BI__builtin_neon_vcagt_v:
  case NEON::BI__builtin_neon_vcagtq_v: {
    // Absolute compare: facge/facgt. The intrinsic takes float-typed vectors.
    mlir::Type floatEleTy;
    unsigned scalarBits =
        mlir::cast<cir::IntType>(vTy.getElementType()).getWidth();
    if (scalarBits == 32)
      floatEleTy = cir::SingleType::get(builder.getContext());
    else if (scalarBits == 64)
      floatEleTy = cir::DoubleType::get(builder.getContext());
    else if (scalarBits == 16)
      floatEleTy = cir::FP16Type::get(builder.getContext());
    else
      llvm_unreachable("unexpected scalar size for vcag/vcal");
    auto vecFloatTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    ops[0] = builder.createBitcast(ops[0], vecFloatTy);
    ops[1] = builder.createBitcast(ops[1], vecFloatTy);
    llvm::StringRef facIntrinsic =
        (builtinID == NEON::BI__builtin_neon_vcage_v ||
         builtinID == NEON::BI__builtin_neon_vcageq_v ||
         builtinID == NEON::BI__builtin_neon_vcale_v ||
         builtinID == NEON::BI__builtin_neon_vcaleq_v)
            ? "aarch64.neon.facge"
            : "aarch64.neon.facgt";
    // Call the intrinsic directly — emitNeonCall would re-bitcast args
    // to argTypes, undoing our float bitcast. Use LLVMIntrinsicCallOp directly.
    mlir::Value res = cir::LLVMIntrinsicCallOp::create(
        builder, getLoc(e->getExprLoc()),
        cgm.getStringAttrCached(facIntrinsic), vTy, ops)
        .getResult();
    return builder.createBitcast(res, convertType(e->getType()));
  }
  case NEON::BI__builtin_neon_vceqz_v:
  case NEON::BI__builtin_neon_vceqzq_v:
  case NEON::BI__builtin_neon_vcgez_v:
  case NEON::BI__builtin_neon_vcgezq_v:
  case NEON::BI__builtin_neon_vcgtz_v:
  case NEON::BI__builtin_neon_vcgtzq_v:
  case NEON::BI__builtin_neon_vclez_v:
  case NEON::BI__builtin_neon_vclezq_v:
  case NEON::BI__builtin_neon_vcltz_v:
  case NEON::BI__builtin_neon_vcltzq_v: {
    // Compare-with-zero: compare element against zero, sext i1 result.
    mlir::Location loc = getLoc(e->getExprLoc());
    // For float comparison, recover the float vector type. The NEON type
    // flags give us an integer vector (ty), but we need to compare as float.
    mlir::Type elemTy = vTy.getElementType();
    bool isFloating = mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(elemTy);
    mlir::Type cmpTy = ty; // integer vector by default
    if (isFloating) {
      // Recover float vector type from element type
      cmpTy = cir::VectorType::get(elemTy, vTy.getSize());
    }
    ops[0] = builder.createBitcast(ops[0], cmpTy);

    cir::CmpOpKind cmpKind;
    switch (builtinID) {
    case NEON::BI__builtin_neon_vceqz_v:
    case NEON::BI__builtin_neon_vceqzq_v:
      cmpKind = cir::CmpOpKind::eq; break;
    case NEON::BI__builtin_neon_vcgez_v:
    case NEON::BI__builtin_neon_vcgezq_v:
      cmpKind = cir::CmpOpKind::ge; break;
    case NEON::BI__builtin_neon_vcgtz_v:
    case NEON::BI__builtin_neon_vcgtzq_v:
      cmpKind = cir::CmpOpKind::gt; break;
    case NEON::BI__builtin_neon_vclez_v:
    case NEON::BI__builtin_neon_vclezq_v:
      cmpKind = cir::CmpOpKind::le; break;
    case NEON::BI__builtin_neon_vcltz_v:
    case NEON::BI__builtin_neon_vcltzq_v:
      cmpKind = cir::CmpOpKind::lt; break;
    default: llvm_unreachable("unexpected compare-with-zero builtin");
    }

    mlir::Value zero = builder.getNullValue(cmpTy, loc);
    auto boolVecTy = cir::VectorType::get(
        cir::BoolType::get(builder.getContext()), vTy.getSize());
    mlir::Value cmp = cir::VecCmpOp::create(builder, loc, boolVecTy,
                                             cmpKind, ops[0], zero);
    // NEON compare returns all-ones (-1) for true. Use a signed integer
    // vector as destination to ensure sext (not zext) in lowering.
    // For float compares, create an int vector with matching element width.
    mlir::Type resTy;
    if (isFloating) {
      unsigned elemBits;
      if (mlir::isa<cir::SingleType>(elemTy))
        elemBits = 32;
      else if (mlir::isa<cir::DoubleType>(elemTy))
        elemBits = 64;
      else
        elemBits = 16; // FP16
      resTy = cir::VectorType::get(builder.getSIntNTy(elemBits), vTy.getSize());
    } else {
      auto signedEleTy = cir::IntType::get(
          builder.getContext(),
          mlir::cast<cir::IntType>(vTy.getElementType()).getWidth(),
          /*isSigned=*/true);
      resTy = cir::VectorType::get(signedEleTy, vTy.getSize());
    }
    mlir::Value res = builder.createCast(cir::CastKind::bool_to_int, cmp,
                                          resTy);
    return builder.createBitcast(res, convertType(e->getType()));
  }
  case NEON::BI__builtin_neon_vcvt_n_f32_v:
  case NEON::BI__builtin_neon_vcvt_n_f64_v:
  case NEON::BI__builtin_neon_vcvtq_n_f32_v:
  case NEON::BI__builtin_neon_vcvtq_n_f64_v: {
    llvm::StringRef intrName = isUnsigned ? "aarch64.neon.vcvtfxu2fp"
                                          : "aarch64.neon.vcvtfxs2fp";
    // NeonTypeFlags encode the INTEGER type (Int32/Int64), not the float type.
    // Derive the float type from the integer element width.
    mlir::Type floatEleTy;
    auto intEltTy = mlir::cast<cir::IntType>(vTy.getElementType());
    switch (intEltTy.getWidth()) {
    case 32:
      floatEleTy = cir::SingleType::get(builder.getContext());
      break;
    case 64:
      floatEleTy = cir::DoubleType::get(builder.getContext());
      break;
    default:
      llvm_unreachable("unexpected integer width for vcvt_n_f");
    }
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    // Only bitcast ops[0] (the integer vector); ops[1] is a scalar i32 shift
    // that must pass through as-is (emitNeonShiftVector would incorrectly
    // splat it into a vector).
    return emitNeonCallCached(*this, {vTy}, ops, intrName, floatVecTy,
                              getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvt_n_f16_s16:
  case NEON::BI__builtin_neon_vcvt_n_f16_u16:
  case NEON::BI__builtin_neon_vcvtq_n_f16_s16:
  case NEON::BI__builtin_neon_vcvtq_n_f16_u16: {
    llvm::StringRef intrName = isUnsigned ? "aarch64.neon.vcvtfxu2fp"
                                          : "aarch64.neon.vcvtfxs2fp";
    auto floatEleTy = cir::FP16Type::get(builder.getContext());
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    // Only bitcast ops[0]; ops[1] is a scalar i32 shift that passes through.
    return emitNeonCallCached(*this, {vTy}, ops, intrName, floatVecTy,
                              getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vrnda_v:
  case NEON::BI__builtin_neon_vrndaq_v:
    intrincsName = "round"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vrnd_v:
  case NEON::BI__builtin_neon_vrndq_v:
    intrincsName = "trunc"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vrndm_v:
  case NEON::BI__builtin_neon_vrndmq_v:
    intrincsName = "floor"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vrndn_v:
  case NEON::BI__builtin_neon_vrndnq_v:
    intrincsName = "roundeven"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vrndp_v:
  case NEON::BI__builtin_neon_vrndpq_v:
    intrincsName = "ceil"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vrndx_v:
  case NEON::BI__builtin_neon_vrndxq_v:
    intrincsName = "rint"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vrndi_v:
  case NEON::BI__builtin_neon_vrndiq_v:
    intrincsName = "nearbyint"; argTypes.push_back(vTy); break;
  case NEON::BI__builtin_neon_vqdmull_v: {
    intrincsName = "aarch64.neon.sqdmull";
    // vqdmull widens: ops are narrow vectors, result is wide.
    // argTypes must describe the narrow input type for both operands.
    auto wideIntTy = mlir::cast<cir::IntType>(vTy.getElementType());
    auto narrowIntTy = cir::IntType::get(builder.getContext(),
                                         wideIntTy.getWidth() / 2,
                                         wideIntTy.isSigned());
    auto narrowTy = cir::VectorType::get(narrowIntTy, vTy.getSize());
    argTypes = {narrowTy, narrowTy};
    break;
  }
  case NEON::BI__builtin_neon_vcvt_n_s16_f16:
  case NEON::BI__builtin_neon_vcvt_n_s32_v:
  case NEON::BI__builtin_neon_vcvt_n_s64_v:
  case NEON::BI__builtin_neon_vcvt_n_u16_f16:
  case NEON::BI__builtin_neon_vcvt_n_u32_v:
  case NEON::BI__builtin_neon_vcvt_n_u64_v:
  case NEON::BI__builtin_neon_vcvtq_n_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_n_s32_v:
  case NEON::BI__builtin_neon_vcvtq_n_s64_v:
  case NEON::BI__builtin_neon_vcvtq_n_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_n_u32_v:
  case NEON::BI__builtin_neon_vcvtq_n_u64_v: {
    // NeonTypeFlags encode the INTEGER result type, not the float input type.
    // Derive the float type from the integer element width.
    mlir::Type floatEleTy;
    auto intEltTy = mlir::cast<cir::IntType>(vTy.getElementType());
    switch (intEltTy.getWidth()) {
    case 16:
      floatEleTy = cir::FP16Type::get(builder.getContext());
      break;
    case 32:
      floatEleTy = cir::SingleType::get(builder.getContext());
      break;
    case 64:
      floatEleTy = cir::DoubleType::get(builder.getContext());
      break;
    default:
      llvm_unreachable("unexpected integer width for vcvt_n");
    }
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    llvm::StringRef intrName = isUnsigned ? "aarch64.neon.vcvtfp2fxu"
                                          : "aarch64.neon.vcvtfp2fxs";
    // Only bitcast ops[0] (the float vector); ops[1] is a scalar i32 shift
    // that must pass through as-is.
    return emitNeonCallCached(*this, {floatVecTy}, ops, intrName, vTy,
                              getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvt_f16_f32: {
    // Float32→float16 narrowing via aarch64.neon.vcvtfp2hf intrinsic.
    // Returns <4 x i16> (8 bytes), bitcast to float16x4_t by arm_neon.h.
    NeonTypeFlags srcFlag = NeonTypeFlags(NeonTypeFlags::Float32, false, true);
    auto srcTy = GetNeonType(this, srcFlag);
    ops[0] = builder.createBitcast(ops[0], srcTy);
    auto i16VecTy = cir::VectorType::get(
        cir::IntType::get(builder.getContext(), 16, true), 4);
    return emitNeonCallCached(*this, {srcTy}, ops, "aarch64.neon.vcvtfp2hf",
                              i16VecTy, getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvt_f32_f16: {
    // Float16→float32 widening via aarch64.neon.vcvthf2fp intrinsic.
    // Intrinsic signature: <4 x float> @llvm.aarch64.neon.vcvthf2fp(<4 x i16>)
    auto i16VecTy = cir::VectorType::get(
        cir::IntType::get(builder.getContext(), 16, true), 4);
    ops[0] = builder.createBitcast(ops[0], i16VecTy);
    auto f32VecTy = cir::VectorType::get(
        cir::SingleType::get(builder.getContext()), 4);
    return emitNeonCallCached(*this, {i16VecTy}, ops, "aarch64.neon.vcvthf2fp",
                              f32VecTy, getLoc(e->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vabs_v:
  case NEON::BI__builtin_neon_vabsq_v: {
    // vabs: absolute value. Float uses fabs intrinsic, int uses aarch64.neon.abs.
    if (mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(
            vTy.getElementType()))
      intrincsName = "fabs";
    else
      intrincsName = "aarch64.neon.abs";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vmmlaq_s32: {
    intrincsName = "aarch64.neon.smmla";
    // vmmlaq takes 3 args: accumulator (i32 vec) + 2 i8 input vectors.
    // All 3 must be listed in argTypes for proper bitcasting.
    auto i8Ty = cir::IntType::get(builder.getContext(), 8, true);
    unsigned numBytes = vTy.getSize() * 4; // 4 bytes per i32 element
    auto i8VecTy = cir::VectorType::get(i8Ty, numBytes);
    argTypes = {vTy, i8VecTy, i8VecTy};
    break;
  }
  case NEON::BI__builtin_neon_vmmlaq_u32: {
    intrincsName = "aarch64.neon.ummla";
    auto u8Ty = cir::IntType::get(builder.getContext(), 8, false);
    unsigned numBytes = vTy.getSize() * 4;
    auto u8VecTy = cir::VectorType::get(u8Ty, numBytes);
    argTypes = {vTy, u8VecTy, u8VecTy};
    break;
  }
  case NEON::BI__builtin_neon_vusmmlaq_s32: {
    // Mixed-sign i8 matrix-multiply-accumulate. usmmla is overloaded on
    // {result, i8-input}; the two i8 operands must keep their <16 x i8> type
    // rather than being bitcast to the i32 result type, which would spell the
    // non-existent usmmla.v4i32.v4i32 overload and fail to select.
    intrincsName = "aarch64.neon.usmmla";
    unsigned numBytes = vTy.getSize() * 4; // 4 bytes per i32 element
    auto i8VecTy = cir::VectorType::get(builder.getSInt8Ty(), numBytes);
    argTypes = {vTy, i8VecTy, i8VecTy};
    break;
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    // Float to integer conversion: fptoui/fptosi.
    mlir::Type floatEleTy;
    if (neonType.getEltType() == NeonTypeFlags::Float16)
      floatEleTy = cir::FP16Type::get(builder.getContext());
    else if (neonType.getEltType() == NeonTypeFlags::Float32)
      floatEleTy = cir::SingleType::get(builder.getContext());
    else
      floatEleTy = cir::DoubleType::get(builder.getContext());
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    ops[0] = builder.createBitcast(ops[0], floatVecTy);
    return builder.createCast(cir::CastKind::float_to_int, ops[0], ty);
  }
  case NEON::BI__builtin_neon_vfma_v:
  case NEON::BI__builtin_neon_vfmaq_v: {
    // vfmaq: Fused multiply-accumulate
    // NEON intrinsic: vfmaq(accumulator, multiplicand1, multiplicand2)
    // LLVM intrinsic: fma(multiplicand1, multiplicand2, accumulator)
    // Reorder arguments to match LLVM fma signature
    std::swap(ops[0], ops[1]);
    std::swap(ops[1], ops[2]);
    intrincsName = "fma";
    // FMA takes 3 arguments of the same type
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vaesmcq_u8: {
    intrincsName = "aarch64.crypto.aesmc";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vaesimcq_u8: {
    intrincsName = "aarch64.crypto.aesimc";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vaeseq_u8: {
    intrincsName = "aarch64.crypto.aese";
    break;
  }
  case NEON::BI__builtin_neon_vaesdq_u8: {
    intrincsName = "aarch64.crypto.aesd";
    break;
  }
  // The SHA-1 and SHA-2 rounds and message-schedule updates. Every one of
  // these was already in AArch64SIMDIntrinsicMap but had no emission here, so
  // the map lookup found the entry, this switch fell through it, and the
  // builtin reached the generic "Not Yet Implemented" error. mbedtls' ARMv8
  // SHA-256 path is enough to hit it, which is how it was found.
  //
  // The three-operand ones have to spell out argTypes: the default when it is
  // left empty is two arguments of the result type, which is right for the
  // two-operand schedule updates below and silently wrong for the rest.
  case NEON::BI__builtin_neon_vsha1su1q_u32: {
    intrincsName = "aarch64.crypto.sha1su1";
    break;
  }
  case NEON::BI__builtin_neon_vsha256su0q_u32: {
    intrincsName = "aarch64.crypto.sha256su0";
    break;
  }
  case NEON::BI__builtin_neon_vsha1su0q_u32: {
    intrincsName = "aarch64.crypto.sha1su0";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vsha256hq_u32: {
    intrincsName = "aarch64.crypto.sha256h";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vsha256h2q_u32: {
    intrincsName = "aarch64.crypto.sha256h2";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vsha256su1q_u32: {
    intrincsName = "aarch64.crypto.sha256su1";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  // The SHA-512 family needs +sha3 rather than +crypto, but is otherwise the
  // same shape and was broken the same way.
  case NEON::BI__builtin_neon_vsha512su0q_u64: {
    intrincsName = "aarch64.crypto.sha512su0";
    break;
  }
  case NEON::BI__builtin_neon_vsha512hq_u64: {
    intrincsName = "aarch64.crypto.sha512h";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vsha512h2q_u64: {
    intrincsName = "aarch64.crypto.sha512h2";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vsha512su1q_u64: {
    intrincsName = "aarch64.crypto.sha512su1";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vdot_s32:
  case NEON::BI__builtin_neon_vdotq_s32: {
    // vdot: Dot product of two i8 vectors, accumulating into i32 vector
    // Intrinsic signature: sdot(v2i32 acc, v8i8 a, v8i8 b) -> v2i32
    //                   or sdot(v4i32 acc, v16i8 a, v16i8 b) -> v4i32
    // InputTy is v8i8 for 64-bit result, v16i8 for 128-bit result
    unsigned numBytes = vTy.getSize() * 4; // 4 bytes per i32 element
    auto inputTy = cir::VectorType::get(builder.getSInt8Ty(), numBytes);
    intrincsName = "aarch64.neon.sdot";
    argTypes = {vTy, inputTy, inputTy};
    ops[0] = builder.createBitcast(ops[0], vTy);
    break;
  }
  case NEON::BI__builtin_neon_vdot_u32:
  case NEON::BI__builtin_neon_vdotq_u32: {
    // vdot unsigned: Dot product of two u8 vectors, accumulating into u32
    // Intrinsic signature: udot(v2u32 acc, v8u8 a, v8u8 b) -> v2u32
    //                   or udot(v4u32 acc, v16u8 a, v16u8 b) -> v4u32
    unsigned numBytes = vTy.getSize() * 4;
    auto inputTy = cir::VectorType::get(builder.getUInt8Ty(), numBytes);
    intrincsName = "aarch64.neon.udot";
    argTypes = {vTy, inputTy, inputTy};
    ops[0] = builder.createBitcast(ops[0], vTy);
    break;
  }
  case NEON::BI__builtin_neon_vusdot_s32:
  case NEON::BI__builtin_neon_vusdotq_s32: {
    // Mixed-sign i8 dot-product accumulate (also the base for the
    // vusdot{,q}_lane_s32 header wrappers). Same overload shape as usmmla:
    // the i8 inputs stay <8/16 x i8> so the intrinsic is usdot.v{2,4}i32.v{8,
    // 16}i8; bitcasting them to the i32 result type produces the
    // non-selectable usdot.vNi32.vNi32 overload.
    unsigned numBytes = vTy.getSize() * 4; // 4 bytes per i32 element
    auto inputTy = cir::VectorType::get(builder.getSInt8Ty(), numBytes);
    intrincsName = "aarch64.neon.usdot";
    argTypes = {vTy, inputTy, inputTy};
    ops[0] = builder.createBitcast(ops[0], vTy);
    break;
  }
  case NEON::BI__builtin_neon_veor3q_s8:
  case NEON::BI__builtin_neon_veor3q_s16:
  case NEON::BI__builtin_neon_veor3q_s32:
  case NEON::BI__builtin_neon_veor3q_s64:
  case NEON::BI__builtin_neon_veor3q_u8:
  case NEON::BI__builtin_neon_veor3q_u16:
  case NEON::BI__builtin_neon_veor3q_u32:
  case NEON::BI__builtin_neon_veor3q_u64: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.crypto.eor3u"
                                                    : "aarch64.crypto.eor3s";
    argTypes = {vTy, vTy, vTy};
    break;
  }
  case NEON::BI__builtin_neon_vpadd_v:
  case NEON::BI__builtin_neon_vpaddq_v: {
    intrincsName = mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(
                       vTy.getElementType())
                       ? "aarch64.neon.faddp"
                       : "aarch64.neon.addp";
    break;
  }
  case NEON::BI__builtin_neon_vqadd_v:
  case NEON::BI__builtin_neon_vqaddq_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.uqadd"
                                                    : "aarch64.neon.sqadd";
    break;
  }
  case NEON::BI__builtin_neon_vqdmulh_v:
  case NEON::BI__builtin_neon_vqdmulhq_v: {
    intrincsName = "aarch64.neon.sqdmulh";
    break;
  }
  case NEON::BI__builtin_neon_vqrdmulh_v:
  case NEON::BI__builtin_neon_vqrdmulhq_v: {
    intrincsName = "aarch64.neon.sqrdmulh";
    break;
  }
  case NEON::BI__builtin_neon_vqsub_v:
  case NEON::BI__builtin_neon_vqsubq_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.uqsub"
                                                    : "aarch64.neon.sqsub";
    break;
  }
  case NEON::BI__builtin_neon_vrhadd_v:
  case NEON::BI__builtin_neon_vrhaddq_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.urhadd"
                                                    : "aarch64.neon.srhadd";
    break;
  }
  case NEON::BI__builtin_neon_vrnd32x_f32:
  case NEON::BI__builtin_neon_vrnd32xq_f32:
  case NEON::BI__builtin_neon_vrnd32x_f64:
  case NEON::BI__builtin_neon_vrnd32xq_f64: {
    intrincsName = "aarch64.neon.frint32x";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vrnd64x_f32:
  case NEON::BI__builtin_neon_vrnd64xq_f32:
  case NEON::BI__builtin_neon_vrnd64x_f64:
  case NEON::BI__builtin_neon_vrnd64xq_f64: {
    intrincsName = "aarch64.neon.frint64x";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vrnd32z_f32:
  case NEON::BI__builtin_neon_vrnd32zq_f32:
  case NEON::BI__builtin_neon_vrnd32z_f64:
  case NEON::BI__builtin_neon_vrnd32zq_f64: {
    intrincsName = "aarch64.neon.frint32z";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vrnd64z_f32:
  case NEON::BI__builtin_neon_vrnd64zq_f32:
  case NEON::BI__builtin_neon_vrnd64z_f64:
  case NEON::BI__builtin_neon_vrnd64zq_f64: {
    intrincsName = "aarch64.neon.frint64z";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vshl_v:
  case NEON::BI__builtin_neon_vshlq_v: {
    // Variable shift: negative shift amounts shift right, positive shift left.
    // Must use target-specific intrinsic, not generic ShiftOp which only does
    // left shift and has UB for negative amounts.
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.ushl"
                                                    : "aarch64.neon.sshl";
    break;
  }
  case NEON::BI__builtin_neon_vhadd_v:
  case NEON::BI__builtin_neon_vhaddq_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.uhadd"
                                                    : "aarch64.neon.shadd";
    break;
  }
  case NEON::BI__builtin_neon_vhsub_v:
  case NEON::BI__builtin_neon_vhsubq_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.uhsub"
                                                    : "aarch64.neon.shsub";
    break;
  }
  case NEON::BI__builtin_neon_vqmovn_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.uqxtn"
                                                    : "aarch64.neon.sqxtn";
    argTypes.push_back(builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /* extended */,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned()));
    break;
  }
  case NEON::BI__builtin_neon_vcvtx_f32_v: {
    // Narrowing: f64 → f32. vTy is <2 x double> (from type code) but the
    // result should be <2 x float>. Override vTy and set argTypes correctly.
    intrincsName = "aarch64.neon.fcvtxn";
    argTypes.push_back(vTy); // input: <2 x double>
    auto floatTy = cir::SingleType::get(builder.getContext());
    vTy = cir::VectorType::get(floatTy, vTy.getSize()); // result: <2 x float>
    break;
  }
  case NEON::BI__builtin_neon_vqmovun_v: {
    intrincsName = "aarch64.neon.sqxtun";
    argTypes.push_back(builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /* extended */, true /* signed */));
    break;
  }
  case NEON::BI__builtin_neon_vrshl_v:
  case NEON::BI__builtin_neon_vrshlq_v: {
    intrincsName = (intrinicId != altLLVMIntrinsic) ? "aarch64.neon.urshl"
                                                    : "aarch64.neon.srshl";
    break;
  }
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v: {
    // Use frecpe for floating point, urecpe for integer
    mlir::Type elemTy = vTy.getElementType();
    if (mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(elemTy))
      intrincsName = "aarch64.neon.frecpe";
    else
      intrincsName = "aarch64.neon.urecpe";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vrecps_v:
  case NEON::BI__builtin_neon_vrecpsq_v:
    intrincsName = "aarch64.neon.frecps";
    break;
  case NEON::BI__builtin_neon_vrsqrte_v:
  case NEON::BI__builtin_neon_vrsqrteq_v: {
    mlir::Type elemTy = vTy.getElementType();
    if (mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(elemTy))
      intrincsName = "aarch64.neon.frsqrte";
    else
      intrincsName = "aarch64.neon.ursqrte";
    argTypes.push_back(vTy);
    break;
  }
  case NEON::BI__builtin_neon_vrsqrts_v:
  case NEON::BI__builtin_neon_vrsqrtsq_v:
    intrincsName = "aarch64.neon.frsqrts";
    break;
  case NEON::BI__builtin_neon_vld1_x2_v:
  case NEON::BI__builtin_neon_vld1q_x2_v: {
    // vld1_x2: Load 2 vectors (non-interleaved)
    mlir::Location loc = getLoc(e->getExprLoc());

    // Create struct type: { vec, vec }
    llvm::SmallVector<mlir::Type, 2> members = {vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    llvm::SmallVector<mlir::Type> ldArgTypes = {ops[1].getType()};
    llvm::SmallVector<mlir::Value> ldArgs = {ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(ldArgTypes),
                                            ldArgs, "aarch64.neon.ld1x2",
                                            structTy, loc);

    // Bitcast pointer to match the struct type and store
    auto ptrTy = cir::PointerType::get(builder.getContext(), structTy);
    mlir::Value storePtr = builder.createBitcast(ops[0], ptrTy);
    (void)cir::StoreOp::create(static_cast<mlir::OpBuilder &>(builder), loc,
                               result, storePtr,
                               /*is_volatile=*/false, /*is_nontemporal=*/false,
                               /*alignment=*/mlir::IntegerAttr{},
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
    return result;
  }
  case NEON::BI__builtin_neon_vld1_x3_v:
  case NEON::BI__builtin_neon_vld1q_x3_v: {
    // vld1_x3: Load 3 vectors (non-interleaved)
    mlir::Location loc = getLoc(e->getExprLoc());

    // Create struct type: { vec, vec, vec }
    llvm::SmallVector<mlir::Type, 3> members = {vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    llvm::SmallVector<mlir::Type> ldArgTypes = {ops[1].getType()};
    llvm::SmallVector<mlir::Value> ldArgs = {ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(ldArgTypes),
                                            ldArgs, "aarch64.neon.ld1x3",
                                            structTy, loc);

    // Bitcast pointer to match the struct type and store
    auto ptrTy = cir::PointerType::get(builder.getContext(), structTy);
    mlir::Value storePtr = builder.createBitcast(ops[0], ptrTy);
    (void)cir::StoreOp::create(static_cast<mlir::OpBuilder &>(builder), loc,
                               result, storePtr,
                               /*is_volatile=*/false, /*is_nontemporal=*/false,
                               /*alignment=*/mlir::IntegerAttr{},
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
    return result;
  }
  case NEON::BI__builtin_neon_vld1_x4_v:
  case NEON::BI__builtin_neon_vld1q_x4_v: {
    // vld1_x4: Load 4 vectors (non-interleaved)
    mlir::Location loc = getLoc(e->getExprLoc());

    // Create struct type: { vec, vec, vec, vec }
    llvm::SmallVector<mlir::Type, 4> members = {vTy, vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    llvm::SmallVector<mlir::Type> ldArgTypes = {ops[1].getType()};
    llvm::SmallVector<mlir::Value> ldArgs = {ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(ldArgTypes),
                                            ldArgs, "aarch64.neon.ld1x4",
                                            structTy, loc);

    // Bitcast pointer to match the struct type and store
    auto ptrTy = cir::PointerType::get(builder.getContext(), structTy);
    mlir::Value storePtr = builder.createBitcast(ops[0], ptrTy);
    (void)cir::StoreOp::create(static_cast<mlir::OpBuilder &>(builder), loc,
                               result, storePtr,
                               /*is_volatile=*/false, /*is_nontemporal=*/false,
                               /*alignment=*/mlir::IntegerAttr{},
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
    return result;
  }
  case NEON::BI__builtin_neon_vst1_x2_v:
  case NEON::BI__builtin_neon_vst1q_x2_v: {
    // vst1_x2: Store 2 vectors (non-interleaved)
    mlir::Location loc = getLoc(e->getExprLoc());

    // Ops layout: [ptr, vec0, vec1]
    mlir::Value vec0 = builder.createBitcast(ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(ops[2], vTy);

    llvm::SmallVector<mlir::Type> stArgTypes = {vec0.getType(), vec1.getType(),
                                                ops[0].getType()};
    llvm::SmallVector<mlir::Value> stArgs = {vec0, vec1, ops[0]};

    return emitNeonCallCached(*this, std::move(stArgTypes), stArgs,
                              "aarch64.neon.st1x2", builder.getVoidTy(), loc);
  }
  case NEON::BI__builtin_neon_vst1_x3_v:
  case NEON::BI__builtin_neon_vst1q_x3_v: {
    // vst1_x3: Store 3 vectors (non-interleaved)
    mlir::Location loc = getLoc(e->getExprLoc());

    // Ops layout: [ptr, vec0, vec1, vec2]
    mlir::Value vec0 = builder.createBitcast(ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(ops[2], vTy);
    mlir::Value vec2 = builder.createBitcast(ops[3], vTy);

    llvm::SmallVector<mlir::Type> stArgTypes = {
        vec0.getType(), vec1.getType(), vec2.getType(), ops[0].getType()};
    llvm::SmallVector<mlir::Value> stArgs = {vec0, vec1, vec2, ops[0]};

    return emitNeonCallCached(*this, std::move(stArgTypes), stArgs,
                              "aarch64.neon.st1x3", builder.getVoidTy(), loc);
  }
  case NEON::BI__builtin_neon_vst1_x4_v:
  case NEON::BI__builtin_neon_vst1q_x4_v: {
    // vst1_x4: Store 4 vectors (non-interleaved)
    mlir::Location loc = getLoc(e->getExprLoc());

    // Ops layout: [ptr, vec0, vec1, vec2, vec3]
    mlir::Value vec0 = builder.createBitcast(ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(ops[2], vTy);
    mlir::Value vec2 = builder.createBitcast(ops[3], vTy);
    mlir::Value vec3 = builder.createBitcast(ops[4], vTy);

    llvm::SmallVector<mlir::Type> stArgTypes = {vec0.getType(), vec1.getType(),
                                                vec2.getType(), vec3.getType(),
                                                ops[0].getType()};
    llvm::SmallVector<mlir::Value> stArgs = {vec0, vec1, vec2, vec3, ops[0]};

    return emitNeonCallCached(*this, std::move(stArgTypes), stArgs,
                              "aarch64.neon.st1x4", builder.getVoidTy(), loc);
  }
  }

  if (intrincsName.empty())
    return nullptr;
  return emitCommonNeonCallPattern0(*this, intrincsName, argTypes, ops, vTy, e);
}

static mlir::Value emitCommonNeonSISDBuiltinExpr(
    CIRGenFunction &cgf, const ARMVectorIntrinsicInfo &info,
    llvm::SmallVectorImpl<mlir::Value> &ops, const CallExpr *expr) {
  unsigned builtinID = info.BuiltinID;
  clang::CIRGen::CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type resultTy = cgf.convertType(expr->getType());
  mlir::Type argTy = cgf.convertType(expr->getArg(0)->getType());
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  switch (builtinID) {
  default:
    llvm::errs() << getAArch64SIMDIntrinsicString(builtinID) << " ";
    llvm_unreachable("in emitCommonNeonSISDBuiltinExpr NYI");
  case NEON::BI__builtin_neon_vabdd_f64:
  case NEON::BI__builtin_neon_vabds_f32:
  case NEON::BI__builtin_neon_vabdh_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.sisd.fabd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vabsd_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.abs", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vaddlv_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.saddlv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vaddlv_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uaddlv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vaddlvq_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.saddlv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vaddlvq_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uaddlv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vaddv_f32:
  case NEON::BI__builtin_neon_vaddvq_f32:
  case NEON::BI__builtin_neon_vaddvq_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.faddv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vaddv_s32:
  case NEON::BI__builtin_neon_vaddvq_s32:
  case NEON::BI__builtin_neon_vaddvq_s64:
  case NEON::BI__builtin_neon_vaddv_u32:
  case NEON::BI__builtin_neon_vaddvq_u32:
  case NEON::BI__builtin_neon_vaddvq_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.add", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vcages_f32:
  case NEON::BI__builtin_neon_vcaged_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.facge",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcagts_f32:
  case NEON::BI__builtin_neon_vcagtd_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.facgt",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcales_f32:
  case NEON::BI__builtin_neon_vcaled_f64:
    // vcale(a,b) = |a| <= |b| = facge(b,a) -- swap operands
    std::swap(ops[0], ops[1]);
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.facge",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcalts_f32:
  case NEON::BI__builtin_neon_vcaltd_f64:
    // vcalt(a,b) = |a| < |b| = facgt(b,a) -- swap operands
    std::swap(ops[0], ops[1]);
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.facgt",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtad_s32_f64:
  case NEON::BI__builtin_neon_vcvtad_s64_f64:
  case NEON::BI__builtin_neon_vcvtas_s32_f32:
  case NEON::BI__builtin_neon_vcvtas_s64_f32:
  case NEON::BI__builtin_neon_vcvtah_s32_f16:
  case NEON::BI__builtin_neon_vcvtah_s64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtas",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtad_u32_f64:
  case NEON::BI__builtin_neon_vcvtad_u64_f64:
  case NEON::BI__builtin_neon_vcvtas_u32_f32:
  case NEON::BI__builtin_neon_vcvtas_u64_f32:
  case NEON::BI__builtin_neon_vcvtah_u32_f16:
  case NEON::BI__builtin_neon_vcvtah_u64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtau",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtd_n_f64_s64:
  case NEON::BI__builtin_neon_vcvts_n_f32_s32:
  case NEON::BI__builtin_neon_vcvth_n_f16_s32:
  case NEON::BI__builtin_neon_vcvth_n_f16_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.vcvtfxs2fp",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtd_n_f64_u64:
  case NEON::BI__builtin_neon_vcvts_n_f32_u32:
  case NEON::BI__builtin_neon_vcvth_n_f16_u32:
  case NEON::BI__builtin_neon_vcvth_n_f16_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.vcvtfxu2fp",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtd_n_s64_f64:
  case NEON::BI__builtin_neon_vcvts_n_s32_f32:
  case NEON::BI__builtin_neon_vcvth_n_s32_f16:
  case NEON::BI__builtin_neon_vcvth_n_s64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.vcvtfp2fxs",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtd_n_u64_f64:
  case NEON::BI__builtin_neon_vcvts_n_u32_f32:
  case NEON::BI__builtin_neon_vcvth_n_u32_f16:
  case NEON::BI__builtin_neon_vcvth_n_u64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.vcvtfp2fxu",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtd_s32_f64:
  case NEON::BI__builtin_neon_vcvtd_s64_f64:
  case NEON::BI__builtin_neon_vcvts_s32_f32:
  case NEON::BI__builtin_neon_vcvts_s64_f32:
  case NEON::BI__builtin_neon_vcvth_s32_f16:
  case NEON::BI__builtin_neon_vcvth_s64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtzs",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtd_u32_f64:
  case NEON::BI__builtin_neon_vcvtd_u64_f64:
  case NEON::BI__builtin_neon_vcvts_u32_f32:
  case NEON::BI__builtin_neon_vcvts_u64_f32:
  case NEON::BI__builtin_neon_vcvth_u32_f16:
  case NEON::BI__builtin_neon_vcvth_u64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtzu",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvth_bf16_f32:
    return builder.createCast(loc, cir::CastKind::floating, ops[0], resultTy);
  case NEON::BI__builtin_neon_vcvtmd_s32_f64:
  case NEON::BI__builtin_neon_vcvtmd_s64_f64:
  case NEON::BI__builtin_neon_vcvtms_s32_f32:
  case NEON::BI__builtin_neon_vcvtms_s64_f32:
  case NEON::BI__builtin_neon_vcvtmh_s32_f16:
  case NEON::BI__builtin_neon_vcvtmh_s64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtms",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtmd_u32_f64:
  case NEON::BI__builtin_neon_vcvtmd_u64_f64:
  case NEON::BI__builtin_neon_vcvtms_u32_f32:
  case NEON::BI__builtin_neon_vcvtms_u64_f32:
  case NEON::BI__builtin_neon_vcvtmh_u32_f16:
  case NEON::BI__builtin_neon_vcvtmh_u64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtmu",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtnd_s32_f64:
  case NEON::BI__builtin_neon_vcvtnd_s64_f64:
  case NEON::BI__builtin_neon_vcvtns_s32_f32:
  case NEON::BI__builtin_neon_vcvtns_s64_f32:
  case NEON::BI__builtin_neon_vcvtnh_s32_f16:
  case NEON::BI__builtin_neon_vcvtnh_s64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtns",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtnd_u32_f64:
  case NEON::BI__builtin_neon_vcvtnd_u64_f64:
  case NEON::BI__builtin_neon_vcvtns_u32_f32:
  case NEON::BI__builtin_neon_vcvtns_u64_f32:
  case NEON::BI__builtin_neon_vcvtnh_u32_f16:
  case NEON::BI__builtin_neon_vcvtnh_u64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtnu",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtpd_s32_f64:
  case NEON::BI__builtin_neon_vcvtpd_s64_f64:
  case NEON::BI__builtin_neon_vcvtps_s32_f32:
  case NEON::BI__builtin_neon_vcvtps_s64_f32:
  case NEON::BI__builtin_neon_vcvtph_s32_f16:
  case NEON::BI__builtin_neon_vcvtph_s64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtps",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtpd_u32_f64:
  case NEON::BI__builtin_neon_vcvtpd_u64_f64:
  case NEON::BI__builtin_neon_vcvtps_u32_f32:
  case NEON::BI__builtin_neon_vcvtps_u64_f32:
  case NEON::BI__builtin_neon_vcvtph_u32_f16:
  case NEON::BI__builtin_neon_vcvtph_u64_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fcvtpu",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vcvtxd_f32_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.sisd.fcvtxn",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vmaxnmv_f32:
  case NEON::BI__builtin_neon_vmaxnmvq_f32:
  case NEON::BI__builtin_neon_vmaxnmvq_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmaxnmv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vmaxv_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmaxv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmaxv_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.smax", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmaxv_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.umax", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmaxvq_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmaxv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmaxvq_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmaxv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmaxvq_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.smax", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmaxvq_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.umax", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vminnmv_f32:
  case NEON::BI__builtin_neon_vminnmvq_f32:
  case NEON::BI__builtin_neon_vminnmvq_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fminnmv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vminv_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fminv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vminv_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.smin", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vminv_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.umin", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vminvq_f32:
  case NEON::BI__builtin_neon_vminvq_f64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fminv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vminvq_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.smin", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vminvq_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.umin", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vmull_p64: {
    // The LLVM intrinsic aarch64.neon.pmull64 has a fixed signature:
    //   (i64, i64) -> <16 x i8>
    // The C-level return type (poly128_t) may differ, so we call the intrinsic
    // with its native return type and bitcast to the expected result type.
    auto intrinResTy = cir::VectorType::get(
        cir::IntType::get(builder.getContext(), 8, /*isSigned=*/false), 16);
    auto result = emitNeonCallCached(cgf, {argTy, argTy}, ops,
                                     "aarch64.neon.pmull64", intrinResTy, loc);
    return builder.createBitcast(result, resultTy);
  }
  case NEON::BI__builtin_neon_vmulxd_f64:
  case NEON::BI__builtin_neon_vmulxs_f32:
  case NEON::BI__builtin_neon_vmulxh_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmulx", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vpaddd_s64:
  case NEON::BI__builtin_neon_vpaddd_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "vector.reduce.add", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vpmaxnmqd_f64:
  case NEON::BI__builtin_neon_vpmaxnms_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmaxnmv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vpmaxqd_f64:
  case NEON::BI__builtin_neon_vpmaxs_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fmaxv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vpminnmqd_f64:
  case NEON::BI__builtin_neon_vpminnms_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fminnmv",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vpminqd_f64:
  case NEON::BI__builtin_neon_vpmins_f32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.fminv", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqabsb_s8: {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqabs", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqabsd_s64:
  case NEON::BI__builtin_neon_vqabss_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqabs", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqabsh_s16: {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqabs", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqaddb_s8: {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqaddb_u8: {
    auto vecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqaddd_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqadd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqaddd_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqadd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqaddh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqaddh_u16:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqadds_s32:
    return builder.createAdd(loc, ops[0], ops[1],
                             cir::OverflowBehavior::Saturated);
  case NEON::BI__builtin_neon_vqadds_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqadd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqdmulhh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf,
        emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqdmulh", vecTy,
                           loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqdmulhs_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqdmulh",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqdmullh_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    vecExtendIntValue(cgf, argVecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqdmull",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqdmulls_s32:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.sqdmulls.scalar",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqmovnd_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.scalar.sqxtn",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqmovnd_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.scalar.uqxtn",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqmovnh_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqxtn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqmovnh_u16:
 {
    auto argVecTy = cir::VectorType::get(cgf.uInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.uqxtn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqmovns_s32: {
    mlir::Location loc = cgf.getLoc(expr->getExprLoc());
    cir::VectorType argVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    cir::VectorType resVecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    mlir::Value result = emitNeonCallCached(cgf, {argVecTy}, ops,
                                            "aarch64.neon.sqxtn", resVecTy,
                                            loc);
    return vecReduceIntValue(cgf, result, loc);
  }
  case NEON::BI__builtin_neon_vqmovns_u32:
 {
    auto argVecTy = cir::VectorType::get(cgf.uInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.uqxtn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqmovund_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.scalar.sqxtun",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqmovunh_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqxtun",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqmovuns_s32:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqxtun",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqnegb_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf,
        emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqneg", vecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqnegd_s64:
  case NEON::BI__builtin_neon_vqnegs_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqneg", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqnegh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf,
        emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqneg", vecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrdmlahh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    vecExtendIntValue(cgf, vecTy, ops[2], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqrdmlah",
                                vecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrdmlahs_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqrdmlah",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqrdmlshh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    vecExtendIntValue(cgf, vecTy, ops[2], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqrdmlsh",
                                vecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrdmlshs_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqrdmlsh",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqrdmulhh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqrdmulh",
                                vecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrdmulhs_s32:
    return emitNeonCallCached(cgf, {resultTy, resultTy}, ops,
                              "aarch64.neon.sqrdmulh", resultTy, loc);
  case NEON::BI__builtin_neon_vqrshlb_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqrshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshlb_u8:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqrshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshld_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqrshl",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqrshld_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqrshl",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqrshlh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqrshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshlh_u16:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqrshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshls_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqrshl",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqrshls_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqrshl",
                              resultTy, loc);
  case NEON::BI__builtin_neon_vqrshrnd_n_s64:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.sqrshrn", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqrshrnd_n_u64:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.uqrshrn", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqrshrnh_n_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqrshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshrnh_n_u16:
 {
    auto argVecTy = cir::VectorType::get(cgf.uInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.uqrshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshrns_n_s32:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqrshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshrns_n_u32:
 {
    auto argVecTy = cir::VectorType::get(cgf.uInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.uqrshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshrund_n_s64:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.sqrshrun", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqrshrunh_n_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqrshrun",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqrshruns_n_s32:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqrshrun",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlb_n_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlb_n_u8:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlb_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlb_u8:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshld_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshld_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshlh_n_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlh_n_u16:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlh_u16:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqshl", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshls_n_s32:
  case NEON::BI__builtin_neon_vqshls_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshls_n_u32:
  case NEON::BI__builtin_neon_vqshls_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshlub_n_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqshlu", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshluh_n_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqshlu", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshlus_n_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqshlu", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshrnd_n_s64:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.sqshrn", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshrnd_n_u64:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.uqshrn", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshrnh_n_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshrnh_n_u16:
 {
    auto argVecTy = cir::VectorType::get(cgf.uInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.uqshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshrns_n_s32:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshrns_n_u32:
 {
    auto argVecTy = cir::VectorType::get(cgf.uInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.uqshrn",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshrund_n_s64:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.neon.sqshrun", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqshrunh_n_s16:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt16Ty, 8);
    auto resVecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqshrun",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqshruns_n_s32:
 {
    auto argVecTy = cir::VectorType::get(cgf.sInt32Ty, 4);
    auto resVecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, argVecTy, ops[0], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {argVecTy}, ops, "aarch64.neon.sqshrun",
                                resVecTy, loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqsubb_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqsub", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqsubb_u8:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqsub", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqsubd_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sqsub", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqsubd_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqsub", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vqsubh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.sqsub", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqsubh_u16:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.uqsub", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vqsubs_s32:
    return builder.createSub(loc, ops[0], ops[1],
                             cir::OverflowBehavior::Saturated);
  case NEON::BI__builtin_neon_vqsubs_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.uqsub", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vrecped_f64:
  case NEON::BI__builtin_neon_vrecpes_f32:
  case NEON::BI__builtin_neon_vrecpeh_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.frecpe", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vrecpxd_f64:
  case NEON::BI__builtin_neon_vrecpxs_f32:
  case NEON::BI__builtin_neon_vrecpxh_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.frecpx", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vrshld_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.srshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vrshld_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.urshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vrsqrted_f64:
  case NEON::BI__builtin_neon_vrsqrtes_f32:
  case NEON::BI__builtin_neon_vrsqrteh_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.frsqrte", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vrsqrtsd_f64:
  case NEON::BI__builtin_neon_vrsqrtss_f32:
  case NEON::BI__builtin_neon_vrsqrtsh_f16:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.frsqrts", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vsha1cq_u32:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.crypto.sha1c", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vsha1h_u32:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.crypto.sha1h", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vsha1mq_u32:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.crypto.sha1m", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vsha1pq_u32:
    return emitNeonCallCached(cgf, {}, ops, "aarch64.crypto.sha1p", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vshld_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.sshl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vshld_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.ushl", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vslid_n_s64:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt64Ty, 1);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.vsli", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vslid_n_u64:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt64Ty, 1);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.vsli", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vsqaddb_u8:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.usqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vsqaddd_u64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.usqadd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vsqaddh_u16:
 {
    auto vecTy = cir::VectorType::get(cgf.uInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.usqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vsqadds_u32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.usqadd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vsrid_n_s64:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt64Ty, 1);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.vsri", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vsrid_n_u64:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt64Ty, 1);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.vsri", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vuqaddb_s8:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt8Ty, 8);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.suqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vuqaddd_s64:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.suqadd", resultTy,
                              loc);
  case NEON::BI__builtin_neon_vuqaddh_s16:
 {
    auto vecTy = cir::VectorType::get(cgf.sInt16Ty, 4);
    vecExtendIntValue(cgf, vecTy, ops[0], loc);
    vecExtendIntValue(cgf, vecTy, ops[1], loc);
    return vecReduceIntValue(
        cgf, emitNeonCallCached(cgf, {vecTy}, ops, "aarch64.neon.suqadd", vecTy,
                                loc),
        loc);
  }
  case NEON::BI__builtin_neon_vuqadds_s32:
    return emitNeonCallCached(cgf, {argTy}, ops, "aarch64.neon.suqadd", resultTy,
                              loc);
  }
  return nullptr;
}

std::optional<mlir::Value>
CIRGenFunction::emitARMBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                   ReturnValueSlot ReturnValue,
                                   llvm::Triple::ArchType Arch) {
  // Only a curated subset of the 32-bit-ARM (AArch32) Advanced SIMD (NEON)
  // __builtin_neon_* family is lowered here so far. The shared
  // emitCommonNeonBuiltinExpr hardwires the aarch64.neon.* intrinsic names, so
  // it cannot be reused directly for AArch32; the cases below instead emit the
  // corresponding arm.neon.* (or target-independent) intrinsics verified
  // identical to classic CodeGen. Anything not handled falls through to nullopt
  // so emitBuiltinExpr reports a loud, safe NYI error rather than miscompiling.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  // Gather every argument except the trailing NeonTypeFlags immediate. The
  // load/store builtins take a pointer as their first argument; capture its
  // Address (for the alignment) the same way classic CodeGen does, rather than
  // evaluating it as a plain scalar.
  llvm::SmallVector<mlir::Value, 4> Ops;
  Address PtrOp0 = Address::invalid();
  Address PtrOp1 = Address::invalid();
  for (unsigned i = 0, e = E->getNumArgs() - 1; i != e; ++i) {
    if (i == 0) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld1_v:
      case NEON::BI__builtin_neon_vld1q_v:
      case NEON::BI__builtin_neon_vld1_dup_v:
      case NEON::BI__builtin_neon_vld1q_dup_v:
      case NEON::BI__builtin_neon_vld1_lane_v:
      case NEON::BI__builtin_neon_vld1q_lane_v:
      case NEON::BI__builtin_neon_vst1_v:
      case NEON::BI__builtin_neon_vst1q_v:
      case NEON::BI__builtin_neon_vst1_lane_v:
      case NEON::BI__builtin_neon_vst1q_lane_v:
      // Non-interleaved multi-vector stores take their destination pointer as
      // arg 0; the vst1xN intrinsic takes no alignment operand, so only the raw
      // pointer is needed.
      case NEON::BI__builtin_neon_vst1_x2_v:
      case NEON::BI__builtin_neon_vst1q_x2_v:
      case NEON::BI__builtin_neon_vst1_x3_v:
      case NEON::BI__builtin_neon_vst1q_x3_v:
      case NEON::BI__builtin_neon_vst1_x4_v:
      case NEON::BI__builtin_neon_vst1q_x4_v:
      // Interleaved stores take their destination pointer as arg 0; capture
      // its Address so the trailing i32 alignment operand can be derived.
      case NEON::BI__builtin_neon_vst2_v:
      case NEON::BI__builtin_neon_vst2q_v:
      case NEON::BI__builtin_neon_vst3_v:
      case NEON::BI__builtin_neon_vst3q_v:
      case NEON::BI__builtin_neon_vst4_v:
      case NEON::BI__builtin_neon_vst4q_v:
      case NEON::BI__builtin_neon_vst2_lane_v:
      case NEON::BI__builtin_neon_vst2q_lane_v:
      case NEON::BI__builtin_neon_vst3_lane_v:
      case NEON::BI__builtin_neon_vst3q_lane_v:
      case NEON::BI__builtin_neon_vst4_lane_v:
      case NEON::BI__builtin_neon_vst4q_lane_v:
        PtrOp0 = emitPointerWithAlignment(E->getArg(0));
        Ops.push_back(PtrOp0.emitRawPointer());
        continue;
      default:
        break;
      }
    }
    if (i == 1) {
      switch (BuiltinID) {
      // Interleaved loads take the aggregate return slot as arg 0 (a plain
      // pointer scalar) and the source data pointer as arg 1; capture the
      // latter's Address for its alignment. The non-interleaved vld1xN forms
      // share the same {retSlot, srcPtr} shape (the vld1xN intrinsic takes no
      // alignment operand, so only the raw source pointer is used).
      case NEON::BI__builtin_neon_vld1_x2_v:
      case NEON::BI__builtin_neon_vld1q_x2_v:
      case NEON::BI__builtin_neon_vld1_x3_v:
      case NEON::BI__builtin_neon_vld1q_x3_v:
      case NEON::BI__builtin_neon_vld1_x4_v:
      case NEON::BI__builtin_neon_vld1q_x4_v:
      case NEON::BI__builtin_neon_vld2_v:
      case NEON::BI__builtin_neon_vld2q_v:
      case NEON::BI__builtin_neon_vld3_v:
      case NEON::BI__builtin_neon_vld3q_v:
      case NEON::BI__builtin_neon_vld4_v:
      case NEON::BI__builtin_neon_vld4q_v:
      case NEON::BI__builtin_neon_vld2_dup_v:
      case NEON::BI__builtin_neon_vld2q_dup_v:
      case NEON::BI__builtin_neon_vld3_dup_v:
      case NEON::BI__builtin_neon_vld3q_dup_v:
      case NEON::BI__builtin_neon_vld4_dup_v:
      case NEON::BI__builtin_neon_vld4q_dup_v:
      case NEON::BI__builtin_neon_vld2_lane_v:
      case NEON::BI__builtin_neon_vld2q_lane_v:
      case NEON::BI__builtin_neon_vld3_lane_v:
      case NEON::BI__builtin_neon_vld3q_lane_v:
      case NEON::BI__builtin_neon_vld4_lane_v:
      case NEON::BI__builtin_neon_vld4q_lane_v:
        PtrOp1 = emitPointerWithAlignment(E->getArg(1));
        Ops.push_back(PtrOp1.emitRawPointer());
        continue;
      default:
        break;
      }
    }
    Ops.push_back(emitScalarOrConstFoldImmArg(ICEArguments, i, E->getArg(i)));
  }

  // NEON lane extract/insert (vget[q]_lane / vset[q]_lane). Their trailing
  // argument is a lane index, not a NeonTypeFlags, so they must be handled
  // before the NeonTypeFlags preamble below. The lowering is target-independent
  // (vector extract/insert), matching emitAArch64BuiltinExpr. The AArch64-only
  // scalar-lane forms (vdup{b,h,s,d}_lane*, f64, mf8, bf16) are deliberately
  // omitted so they remain a loud NYI on AArch32 (classic rejects them there).
  switch (BuiltinID) {
  default:
    break;
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    Ops[0], Ops[2]);
  case NEON::BI__builtin_neon_vget_lane_i8:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt8Ty, 8));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i8:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt8Ty, 16));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_i16:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt16Ty, 4));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i16:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt16Ty, 8));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_i32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt32Ty, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt32Ty, 4));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_i64:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt64Ty, 1));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i64:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt64Ty, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_f32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(floatTy, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_f32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(floatTy, 4));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));

  // f16<->f32 vector conversions. These carry no NeonTypeFlags trailing arg
  // (fixed types), so they are handled here before the NeonTypeFlags preamble.
  case NEON::BI__builtin_neon_vcvt_f16_f32: {
    // f32x4 -> f16x4, returned as <4 x i16> (arm_neon.h bitcasts to float16x4_t).
    NeonTypeFlags srcFlag(NeonTypeFlags::Float32, false, true);
    cir::VectorType srcTy = GetNeonType(this, srcFlag);
    Ops[0] = builder.createBitcast(Ops[0], srcTy);
    cir::VectorType i16VecTy = cir::VectorType::get(
        cir::IntType::get(builder.getContext(), 16, true), 4);
    return emitNeonCallCached(*this, {srcTy}, Ops, "arm.neon.vcvtfp2hf",
                              i16VecTy, getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvt_f32_f16: {
    // f16x4 -> f32x4 via arm.neon.vcvthf2fp(<4 x i16>).
    cir::VectorType i16VecTy = cir::VectorType::get(
        cir::IntType::get(builder.getContext(), 16, true), 4);
    Ops[0] = builder.createBitcast(Ops[0], i16VecTy);
    cir::VectorType f32VecTy =
        cir::VectorType::get(cir::SingleType::get(builder.getContext()), 4);
    return emitNeonCallCached(*this, {i16VecTy}, Ops, "arm.neon.vcvthf2fp",
                              f32VecTy, getLoc(E->getExprLoc()));
  }
  }

  const Expr *arg = E->getArg(E->getNumArgs() - 1);
  std::optional<llvm::APSInt> neonTypeConst =
      arg->getIntegerConstantExpr(getContext());
  if (!neonTypeConst)
    return std::nullopt;

  NeonTypeFlags Type(neonTypeConst->getZExtValue());
  const bool usgn = Type.isUnsigned();
  cir::VectorType vTy =
      GetNeonType(this, Type, getTarget().hasFastHalfType());
  if (!vTy)
    return std::nullopt;
  mlir::Location loc = getLoc(E->getExprLoc());

  auto binIntrinsic = [&](llvm::StringRef name) -> mlir::Value {
    return emitNeonCallCached(*this, {vTy, vTy}, Ops, name, vTy, loc);
  };

  switch (BuiltinID) {
  default:
    return std::nullopt;

  // Elementwise integer min/max/abs-difference and (rounding) halving add.
  case NEON::BI__builtin_neon_vmax_v:
  case NEON::BI__builtin_neon_vmaxq_v:
    return binIntrinsic(usgn ? "arm.neon.vmaxu" : "arm.neon.vmaxs");
  case NEON::BI__builtin_neon_vmin_v:
  case NEON::BI__builtin_neon_vminq_v:
    return binIntrinsic(usgn ? "arm.neon.vminu" : "arm.neon.vmins");
  case NEON::BI__builtin_neon_vabd_v:
  case NEON::BI__builtin_neon_vabdq_v:
    return binIntrinsic(usgn ? "arm.neon.vabdu" : "arm.neon.vabds");
  case NEON::BI__builtin_neon_vhadd_v:
  case NEON::BI__builtin_neon_vhaddq_v:
    return binIntrinsic(usgn ? "arm.neon.vhaddu" : "arm.neon.vhadds");
  case NEON::BI__builtin_neon_vrhadd_v:
  case NEON::BI__builtin_neon_vrhaddq_v:
    return binIntrinsic(usgn ? "arm.neon.vrhaddu" : "arm.neon.vrhadds");

  // Register-controlled shifts (signed/unsigned, plain/rounding/saturating).
  case NEON::BI__builtin_neon_vshl_v:
  case NEON::BI__builtin_neon_vshlq_v:
    return binIntrinsic(usgn ? "arm.neon.vshiftu" : "arm.neon.vshifts");
  case NEON::BI__builtin_neon_vrshl_v:
  case NEON::BI__builtin_neon_vrshlq_v:
    return binIntrinsic(usgn ? "arm.neon.vrshiftu" : "arm.neon.vrshifts");
  case NEON::BI__builtin_neon_vqshl_v:
  case NEON::BI__builtin_neon_vqshlq_v:
    return binIntrinsic(usgn ? "arm.neon.vqshiftu" : "arm.neon.vqshifts");
  case NEON::BI__builtin_neon_vqrshl_v:
  case NEON::BI__builtin_neon_vqrshlq_v:
    return binIntrinsic(usgn ? "arm.neon.vqrshiftu" : "arm.neon.vqrshifts");

  // Saturating add/sub lower to the target-independent saturating intrinsics.
  case NEON::BI__builtin_neon_vqadd_v:
  case NEON::BI__builtin_neon_vqaddq_v:
    return binIntrinsic(usgn ? "uadd.sat" : "sadd.sat");
  case NEON::BI__builtin_neon_vqsub_v:
  case NEON::BI__builtin_neon_vqsubq_v:
    return binIntrinsic(usgn ? "usub.sat" : "ssub.sat");

  // IEEE floating-point min/max (NaN-propagating variants).
  case NEON::BI__builtin_neon_vmaxnm_v:
  case NEON::BI__builtin_neon_vmaxnmq_v:
    return binIntrinsic("arm.neon.vmaxnm");
  case NEON::BI__builtin_neon_vminnm_v:
  case NEON::BI__builtin_neon_vminnmq_v:
    return binIntrinsic("arm.neon.vminnm");

  // Pairwise min/max (AArch32 pairwise ops are D-register only, no q form).
  // Float uses the signed intrinsic name (usgn is false for float NeonTypes).
  case NEON::BI__builtin_neon_vpmax_v:
    return binIntrinsic(usgn ? "arm.neon.vpmaxu" : "arm.neon.vpmaxs");
  case NEON::BI__builtin_neon_vpmin_v:
    return binIntrinsic(usgn ? "arm.neon.vpminu" : "arm.neon.vpmins");

  // Halving subtract.
  case NEON::BI__builtin_neon_vhsub_v:
  case NEON::BI__builtin_neon_vhsubq_v:
    return binIntrinsic(usgn ? "arm.neon.vhsubu" : "arm.neon.vhsubs");

  // Polynomial multiply (GF(2) mul, e.g. CRC). The vmul_v builtin is ONLY
  // generated for poly types (integer/float vmul are arm_neon.h OP-codes
  // emitting plain a*b); non-poly reaching here stays a loud NYI.
  case NEON::BI__builtin_neon_vmul_v:
  case NEON::BI__builtin_neon_vmulq_v:
    if (Type.isPoly())
      return binIntrinsic("arm.neon.vmulp");
    return std::nullopt;

  // Polynomial add over GF(2) is XOR (vadd_v builtin is only generated for
  // poly; integer/float vadd are arm_neon.h OP-codes emitting plain a+b).
  case NEON::BI__builtin_neon_vadd_v:
  case NEON::BI__builtin_neon_vaddq_v:
    if (Type.isPoly()) {
      Ops[0] = builder.createBitcast(Ops[0], vTy);
      Ops[1] = builder.createBitcast(Ops[1], vTy);
      return builder.createBinop(loc, Ops[0], cir::BinOpKind::Xor, Ops[1]);
    }
    return std::nullopt;

  // Immediate shifts lower to generic vector shift ops.
  case NEON::BI__builtin_neon_vshl_n_v:
  case NEON::BI__builtin_neon_vshlq_n_v:
    return emitCommonNeonShift(builder, loc, vTy, Ops[0], Ops[1], true);
  case NEON::BI__builtin_neon_vshr_n_v:
  case NEON::BI__builtin_neon_vshrq_n_v:
    return emitNeonRShiftImm(*this, Ops[0], Ops[1], vTy, usgn, loc);

  // Rounding shift-right by immediate. The shift amount (Ops[1]) is negated by
  // emitNeonCallCached (rightshift=true) to match the intrinsic's convention.
  case NEON::BI__builtin_neon_vrshr_n_v:
  case NEON::BI__builtin_neon_vrshrq_n_v:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops,
                              usgn ? "arm.neon.vrshiftu" : "arm.neon.vrshifts",
                              vTy, loc, /*isConstrainedFPIntrinsic=*/false,
                              /*shift=*/1, /*rightshift=*/true);

  // Saturating shift-left by immediate. vqshlu treats the input as signed and
  // the result as unsigned through the single arm.neon.vqshiftsu intrinsic
  // (no input sign change, unlike AArch64's sqshlu).
  case NEON::BI__builtin_neon_vqshl_n_v:
  case NEON::BI__builtin_neon_vqshlq_n_v:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops,
                              usgn ? "arm.neon.vqshiftu" : "arm.neon.vqshifts",
                              vTy, loc, /*isConstrainedFPIntrinsic=*/false,
                              /*shift=*/1, /*rightshift=*/false);
  case NEON::BI__builtin_neon_vqshlu_n_v:
  case NEON::BI__builtin_neon_vqshluq_n_v:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops, "arm.neon.vqshiftsu", vTy,
                              loc, /*isConstrainedFPIntrinsic=*/false,
                              /*shift=*/1, /*rightshift=*/false);

  // Unary lane-wise operations.
  case NEON::BI__builtin_neon_vabs_v:
  case NEON::BI__builtin_neon_vabsq_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    if (mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(
            vTy.getElementType()))
      return cir::FAbsOp::create(builder, loc, Ops[0]);
    return emitNeonCallCached(*this, {vTy}, Ops, "arm.neon.vabs", vTy, loc);
  }
  case NEON::BI__builtin_neon_vqabs_v:
  case NEON::BI__builtin_neon_vqabsq_v:
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    return emitNeonCallCached(*this, {vTy}, Ops, "arm.neon.vqabs", vTy, loc);
  case NEON::BI__builtin_neon_vqneg_v:
  case NEON::BI__builtin_neon_vqnegq_v:
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    return emitNeonCallCached(*this, {vTy}, Ops, "arm.neon.vqneg", vTy, loc);
  case NEON::BI__builtin_neon_vcls_v:
  case NEON::BI__builtin_neon_vclsq_v:
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    return emitNeonCallCached(*this, {vTy}, Ops, "arm.neon.vcls", vTy, loc);
  case NEON::BI__builtin_neon_vclz_v:
  case NEON::BI__builtin_neon_vclzq_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    mlir::Value isZeroPoison = getTarget().isCLZForZeroUndef()
                                   ? builder.getTrue(loc)
                                   : builder.getFalse(loc);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "ctlz", vTy,
                                     mlir::ValueRange{Ops[0], isZeroPoison});
  }
  case NEON::BI__builtin_neon_vcnt_v:
  case NEON::BI__builtin_neon_vcntq_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    return emitIntrinsicCallOpCached(cgm, builder, loc, "ctpop", vTy,
                                     mlir::ValueRange{Ops[0]});
  }

  // Contiguous vector load/store. On AArch32 the plain vld1/vst1 lower to the
  // @llvm.arm.neon.vld1/vst1 intrinsics (unlike AArch64, which uses a plain
  // aligned load/store); the extra i32 operand carries the access alignment.
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    Ops.push_back(builder.getConstInt(
        loc, builder.getUInt32Ty(), PtrOp0.getAlignment().getQuantity()));
    return emitNeonCallCached(*this, {}, Ops, "arm.neon.vld1", vTy, loc);
  }
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v: {
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    Ops.push_back(builder.getConstInt(
        loc, builder.getUInt32Ty(), PtrOp0.getAlignment().getQuantity()));
    cir::LLVMIntrinsicCallOp::create(builder, loc,
                                     cgm.getStringAttrCached("arm.neon.vst1"),
                                     mlir::Type{}, mlir::ValueRange{Ops});
    return nullptr;
  }

  // Load a single element into one lane of an existing vector: a scalar load
  // followed by a vector insert (same shape as classic CodeGen for non-i64
  // elements). The i64-quad special case uses a one-element-vector intrinsic
  // path in classic CodeGen and is left NYI for now.
  case NEON::BI__builtin_neon_vld1_lane_v:
  case NEON::BI__builtin_neon_vld1q_lane_v: {
    auto eltIntTy = mlir::dyn_cast<cir::IntType>(vTy.getElementType());
    if (BuiltinID == NEON::BI__builtin_neon_vld1q_lane_v && eltIntTy &&
        eltIntTy.getWidth() == 64)
      return std::nullopt;
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    mlir::Value ptr = Ops[0];
    if (vTy.getElementType() !=
        mlir::cast<cir::PointerType>(ptr.getType()).getPointee())
      ptr = builder.createPtrBitcast(ptr, vTy.getElementType());
    mlir::Value loadedVal = builder.createAlignedLoad(
        ptr.getLoc(), vTy.getElementType(), ptr, PtrOp0.getAlignment());
    return mlir::Value{
        cir::VecInsertOp::create(builder, loc, Ops[1], loadedVal, Ops[2])};
  }

  // Load one element and duplicate it across every lane: scalar load + splat.
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Address ptrAddr = PtrOp0.withElementType(builder, vTy.getElementType());
    mlir::Value val = builder.createLoad(loc, ptrAddr);
    return mlir::Value{cir::VecSplatOp::create(builder, loc, vTy, val)};
  }

  // Store one lane of a vector to memory. For non-i64 elements this is a vector
  // extract followed by a scalar store. For a 64-bit integer lane of a Q
  // (128-bit) vector, classic CodeGen instead shuffles the selected lane into a
  // one-element <1 x i64> vector and stores it via @llvm.arm.neon.vst1 (with an
  // i32 alignment operand) to avoid poor i64 backend code; mirror that. The i64
  // Q vld1q_lane_v load still uses a one-element-vector intrinsic path and is
  // left NYI.
  case NEON::BI__builtin_neon_vst1_lane_v:
  case NEON::BI__builtin_neon_vst1q_lane_v: {
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    auto eltIntTy = mlir::dyn_cast<cir::IntType>(vTy.getElementType());
    if (BuiltinID == NEON::BI__builtin_neon_vst1q_lane_v && eltIntTy &&
        eltIntTy.getWidth() == 64) {
      llvm::SmallVector<int64_t, 1> mask{getIntValueFromConstOp(Ops[2])};
      mlir::Value oneElt = builder.createVecShuffle(loc, Ops[1], Ops[1], mask);
      llvm::SmallVector<mlir::Value, 3> stOps{
          Ops[0], oneElt,
          builder.getConstInt(loc, builder.getUInt32Ty(),
                              PtrOp0.getAlignment().getQuantity())};
      cir::LLVMIntrinsicCallOp::create(builder, loc,
                                       cgm.getStringAttrCached("arm.neon.vst1"),
                                       mlir::Type{}, mlir::ValueRange{stOps});
      return nullptr;
    }
    Ops[1] = cir::VecExtractOp::create(builder, loc, Ops[1], Ops[2]);
    Address storeAddr = PtrOp0.withElementType(builder, vTy.getElementType());
    (void)builder.createAlignedStore(loc, Ops[1], storeAddr.getPointer(),
                                     PtrOp0.getAlignment());
    return nullptr;
  }

  // Interleaved (de-interleaved) multi-vector load. The arm.neon.vld{2,3,4}
  // (and their _dup) intrinsics take the source pointer plus an i32 alignment
  // and RETURN an aggregate of {2,3,4} vectors; that aggregate is stored
  // through the return-slot pointer (Ops[0]) exactly as classic CodeGen does.
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v:
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v:
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v:
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v:
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v:
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    unsigned numVecs;
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vld2_v:
    case NEON::BI__builtin_neon_vld2q_v:
      numVecs = 2;
      name = "arm.neon.vld2";
      break;
    case NEON::BI__builtin_neon_vld3_v:
    case NEON::BI__builtin_neon_vld3q_v:
      numVecs = 3;
      name = "arm.neon.vld3";
      break;
    case NEON::BI__builtin_neon_vld4_v:
    case NEON::BI__builtin_neon_vld4q_v:
      numVecs = 4;
      name = "arm.neon.vld4";
      break;
    case NEON::BI__builtin_neon_vld2_dup_v:
    case NEON::BI__builtin_neon_vld2q_dup_v:
      numVecs = 2;
      name = "arm.neon.vld2dup";
      break;
    case NEON::BI__builtin_neon_vld3_dup_v:
    case NEON::BI__builtin_neon_vld3q_dup_v:
      numVecs = 3;
      name = "arm.neon.vld3dup";
      break;
    default:
      numVecs = 4;
      name = "arm.neon.vld4dup";
      break;
    }
    llvm::SmallVector<mlir::Type, 4> members(numVecs, vTy);
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);
    mlir::Value align = builder.getConstInt(
        loc, builder.getUInt32Ty(), PtrOp1.getAlignment().getQuantity());
    llvm::SmallVector<mlir::Type> ldArgTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> ldArgs = {Ops[1], align};
    mlir::Value result = emitNeonCallCached(*this, std::move(ldArgTypes),
                                            ldArgs, name, structTy, loc);
    auto ptrTy = cir::PointerType::get(builder.getContext(), structTy);
    mlir::Value storePtr = builder.createBitcast(Ops[0], ptrTy);
    // Match classic CreateDefaultAlignedStore. On AArch32 a NEON vector (and
    // thus the multi-vector aggregate) is at most 8-byte (doubleword) aligned
    // per the target datalayout's v128:64 rule; CIR's type layout doesn't model
    // that cap, so clamp the aggregate ABI alignment to 8.
    uint64_t abiAlign = cgm.getDataLayout().getABITypeAlign(structTy).value();
    if (abiAlign > 8)
      abiAlign = 8;
    (void)builder.createAlignedStore(
        loc, result, storePtr, clang::CharUnits::fromQuantity(abiAlign));
    return result;
  }

  // Interleaved load of a single lane into pre-existing vectors. Same aggregate
  // return/store shape as vld{2,3,4}, but the intrinsic also takes the current
  // vectors and an i32 lane index before the alignment.
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v:
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v:
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    unsigned numVecs;
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vld2_lane_v:
    case NEON::BI__builtin_neon_vld2q_lane_v:
      numVecs = 2;
      name = "arm.neon.vld2lane";
      break;
    case NEON::BI__builtin_neon_vld3_lane_v:
    case NEON::BI__builtin_neon_vld3q_lane_v:
      numVecs = 3;
      name = "arm.neon.vld3lane";
      break;
    default:
      numVecs = 4;
      name = "arm.neon.vld4lane";
      break;
    }
    llvm::SmallVector<mlir::Type, 4> members(numVecs, vTy);
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);
    mlir::Value align = builder.getConstInt(
        loc, builder.getUInt32Ty(), PtrOp1.getAlignment().getQuantity());
    // Ops layout: [retSlot, srcPtr, vec0..vec{N-1}, laneIdx].
    llvm::SmallVector<mlir::Type> ldArgTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> ldArgs = {Ops[1]};
    for (unsigned v = 0; v < numVecs; ++v) {
      ldArgTypes.push_back(vTy);
      ldArgs.push_back(builder.createBitcast(Ops[2 + v], vTy));
    }
    ldArgs.push_back(Ops[2 + numVecs]);
    ldArgs.push_back(align);
    mlir::Value result = emitNeonCallCached(*this, std::move(ldArgTypes),
                                            ldArgs, name, structTy, loc);
    auto ptrTy = cir::PointerType::get(builder.getContext(), structTy);
    mlir::Value storePtr = builder.createBitcast(Ops[0], ptrTy);
    // Match classic CreateDefaultAlignedStore. On AArch32 a NEON vector (and
    // thus the multi-vector aggregate) is at most 8-byte (doubleword) aligned
    // per the target datalayout's v128:64 rule; CIR's type layout doesn't model
    // that cap, so clamp the aggregate ABI alignment to 8.
    uint64_t abiAlign = cgm.getDataLayout().getABITypeAlign(structTy).value();
    if (abiAlign > 8)
      abiAlign = 8;
    (void)builder.createAlignedStore(
        loc, result, storePtr, clang::CharUnits::fromQuantity(abiAlign));
    return result;
  }

  // Interleaved (interleaving) multi-vector store. arm.neon.vst{2,3,4} (and
  // their _lane) take the destination pointer, the {2,3,4} source vectors
  // (already split into separate arguments by the frontend), an optional i32
  // lane index, and a trailing i32 alignment. The store intrinsic overloads
  // its pointer operand first, so the pointer stays at Ops[0].
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v:
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v:
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v:
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v:
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v:
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    unsigned numVecs;
    bool isLane;
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vst2_v:
    case NEON::BI__builtin_neon_vst2q_v:
      numVecs = 2;
      isLane = false;
      name = "arm.neon.vst2";
      break;
    case NEON::BI__builtin_neon_vst3_v:
    case NEON::BI__builtin_neon_vst3q_v:
      numVecs = 3;
      isLane = false;
      name = "arm.neon.vst3";
      break;
    case NEON::BI__builtin_neon_vst4_v:
    case NEON::BI__builtin_neon_vst4q_v:
      numVecs = 4;
      isLane = false;
      name = "arm.neon.vst4";
      break;
    case NEON::BI__builtin_neon_vst2_lane_v:
    case NEON::BI__builtin_neon_vst2q_lane_v:
      numVecs = 2;
      isLane = true;
      name = "arm.neon.vst2lane";
      break;
    case NEON::BI__builtin_neon_vst3_lane_v:
    case NEON::BI__builtin_neon_vst3q_lane_v:
      numVecs = 3;
      isLane = true;
      name = "arm.neon.vst3lane";
      break;
    default:
      numVecs = 4;
      isLane = true;
      name = "arm.neon.vst4lane";
      break;
    }
    mlir::Value align = builder.getConstInt(
        loc, builder.getUInt32Ty(), PtrOp0.getAlignment().getQuantity());
    // Ops layout: [dstPtr, vec0..vec{N-1}, (laneIdx)].
    llvm::SmallVector<mlir::Type> stArgTypes = {Ops[0].getType()};
    llvm::SmallVector<mlir::Value> stArgs = {Ops[0]};
    for (unsigned v = 0; v < numVecs; ++v) {
      stArgTypes.push_back(vTy);
      stArgs.push_back(builder.createBitcast(Ops[1 + v], vTy));
    }
    if (isLane)
      stArgs.push_back(Ops[1 + numVecs]);
    stArgs.push_back(align);
    return emitNeonCallCached(*this, std::move(stArgTypes), stArgs, name,
                              builder.getVoidTy(), loc);
  }

  // Widening multiply: multiply narrow vectors into a double-width result.
  case NEON::BI__builtin_neon_vmull_v: {
    llvm::StringRef name = usgn ? "arm.neon.vmullu" : "arm.neon.vmulls";
    if (Type.isPoly())
      name = "arm.neon.vmullp";
    cir::VectorType argTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /*truncate*/, !usgn);
    return emitNeonCallCached(*this, {argTy, argTy}, Ops, name, vTy, loc);
  }

  // Widen (sext/zext) and narrow (trunc) via generic integer casts.
  case NEON::BI__builtin_neon_vmovl_v: {
    cir::VectorType dTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /*truncate*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    Ops[0] = builder.createBitcast(Ops[0], dTy);
    return builder.createIntCast(Ops[0], vTy);
  }
  case NEON::BI__builtin_neon_vmovn_v: {
    cir::VectorType qTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /*extend*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    Ops[0] = builder.createBitcast(Ops[0], qTy);
    return builder.createIntCast(Ops[0], vTy);
  }

  // Saturating narrow.
  case NEON::BI__builtin_neon_vqmovn_v: {
    cir::VectorType argTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /*extend*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    return emitNeonCallCached(*this, {argTy}, Ops,
                              usgn ? "arm.neon.vqmovnu" : "arm.neon.vqmovns",
                              vTy, loc);
  }
  case NEON::BI__builtin_neon_vqmovun_v: {
    cir::VectorType argTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /*extend*/, true /*signed*/);
    return emitNeonCallCached(*this, {argTy}, Ops, "arm.neon.vqmovnsu", vTy,
                              loc);
  }

  // Add/subtract and narrow-high: (a +/- b) >> (srcEltBits/2), then truncate.
  // vTy is the narrow result type; the op runs on the extended (wide) type.
  case NEON::BI__builtin_neon_vaddhn_v:
  case NEON::BI__builtin_neon_vsubhn_v: {
    bool isSigned = mlir::cast<cir::IntType>(vTy.getElementType()).isSigned();
    cir::VectorType srcTy =
        builder.getExtendedOrTruncatedElementVectorType(vTy, true, isSigned);
    Ops[0] = builder.createBitcast(Ops[0], srcTy);
    Ops[1] = builder.createBitcast(Ops[1], srcTy);
    cir::BinOpKind bok = BuiltinID == NEON::BI__builtin_neon_vaddhn_v
                             ? cir::BinOpKind::Add
                             : cir::BinOpKind::Sub;
    Ops[0] = builder.createBinop(loc, Ops[0], bok, Ops[1]);
    auto srcElemTy = mlir::cast<cir::IntType>(srcTy.getElementType());
    mlir::Value shiftVal =
        builder.getConstInt(loc, srcElemTy, srcElemTy.getWidth() / 2);
    mlir::Value shiftVec =
        emitNeonShiftVector(builder, shiftVal, srcTy, loc, false);
    Ops[0] = builder.createLShr(loc, Ops[0], shiftVec);
    return builder.createIntCast(Ops[0], vTy);
  }

  // Rounding add/subtract and narrow-high (rounding done by the intrinsic).
  case NEON::BI__builtin_neon_vraddhn_v:
  case NEON::BI__builtin_neon_vrsubhn_v: {
    bool isSigned = mlir::cast<cir::IntType>(vTy.getElementType()).isSigned();
    cir::VectorType srcTy =
        builder.getExtendedOrTruncatedElementVectorType(vTy, true, isSigned);
    return emitNeonCallCached(
        *this, {srcTy, srcTy}, Ops,
        BuiltinID == NEON::BI__builtin_neon_vraddhn_v ? "arm.neon.vraddhn"
                                                      : "arm.neon.vrsubhn",
        vTy, loc);
  }

  // Pairwise add, and pairwise add-long / add-and-accumulate-long.
  case NEON::BI__builtin_neon_vpadd_v:
    return binIntrinsic("arm.neon.vpadd");
  case NEON::BI__builtin_neon_vpaddl_v:
  case NEON::BI__builtin_neon_vpaddlq_v: {
    cir::VectorType narrowTy = getHalfEltSizeTwiceNumElemsVecType(builder, vTy);
    return emitNeonCallCached(*this, {narrowTy}, Ops,
                              usgn ? "arm.neon.vpaddlu" : "arm.neon.vpaddls",
                              vTy, loc);
  }
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    cir::VectorType narrowTy = getHalfEltSizeTwiceNumElemsVecType(builder, vTy);
    return emitNeonCallCached(*this, {vTy, narrowTy}, Ops,
                              usgn ? "arm.neon.vpadalu" : "arm.neon.vpadals",
                              vTy, loc);
  }

  // Widening shift-left by immediate: sign/zero-extend the narrow input to the
  // wide result type, then a plain vector shl. No intrinsic (matches classic).
  case NEON::BI__builtin_neon_vshll_n_v: {
    cir::VectorType srcTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /*truncate*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    Ops[0] = builder.createBitcast(Ops[0], srcTy);
    Ops[0] = builder.createIntCast(Ops[0], vTy);
    return emitCommonNeonShift(builder, loc, vTy, Ops[0], Ops[1], true);
  }
  // Narrowing shift-right by immediate: shift the wide input then truncate to
  // the narrow result. No intrinsic (matches classic).
  case NEON::BI__builtin_neon_vshrn_n_v: {
    cir::VectorType srcTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /*extend*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    Ops[0] = builder.createBitcast(Ops[0], srcTy);
    Ops[0] = emitCommonNeonShift(builder, loc, srcTy, Ops[0], Ops[1], false);
    return builder.createIntCast(Ops[0], vTy);
  }

  // Saturating/rounding narrowing shift-right by immediate. These lower to the
  // arm.neon.v{q,qr}shiftn{s,u,su} intrinsics whose result is the narrow type
  // and whose two operands are the wide type; the shift amount is passed as a
  // splatted NEGATIVE constant vector (the arm.neon right-shift convention).
  case NEON::BI__builtin_neon_vqshrn_n_v:
  case NEON::BI__builtin_neon_vqshrun_n_v:
  case NEON::BI__builtin_neon_vrshrn_n_v:
  case NEON::BI__builtin_neon_vqrshrn_n_v:
  case NEON::BI__builtin_neon_vqrshrun_n_v: {
    cir::VectorType wideTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, true /*extend*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vqshrn_n_v:
      name = usgn ? "arm.neon.vqshiftnu" : "arm.neon.vqshiftns";
      break;
    case NEON::BI__builtin_neon_vqshrun_n_v:
      name = "arm.neon.vqshiftnsu";
      break;
    case NEON::BI__builtin_neon_vrshrn_n_v:
      name = "arm.neon.vrshiftn";
      break;
    case NEON::BI__builtin_neon_vqrshrn_n_v:
      name = usgn ? "arm.neon.vqrshiftnu" : "arm.neon.vqrshiftns";
      break;
    case NEON::BI__builtin_neon_vqrshrun_n_v:
      name = "arm.neon.vqrshiftnsu";
      break;
    }
    return emitNeonCallCached(*this, {wideTy, wideTy}, Ops, name, vTy, loc,
                              /*isConstrainedFPIntrinsic=*/false,
                              /*shift=*/1, /*rightshift=*/true);
  }

  // Shift-left/right and insert: arm.neon.vshiftins(dst, src, amount). vsri
  // passes a NEGATIVE amount (right shift); vsli a positive one.
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v:
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v: {
    bool rightShift = BuiltinID == NEON::BI__builtin_neon_vsri_n_v ||
                      BuiltinID == NEON::BI__builtin_neon_vsriq_n_v;
    return emitNeonCallCached(*this, {vTy, vTy, vTy}, Ops, "arm.neon.vshiftins",
                              vTy, loc, /*isConstrainedFPIntrinsic=*/false,
                              /*shift=*/2, /*rightshift=*/rightShift);
  }

  // Shift-right by immediate and accumulate: (a + (b >> n)). Plain lshr/ashr.
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = emitNeonRShiftImm(*this, Ops[1], Ops[2], vTy, usgn, loc);
    return builder.createAdd(loc, Ops[0], Ops[1]);
  }
  // Rounding shift-right by immediate and accumulate: (a + vrshift(b, -n)).
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    llvm::SmallVector<mlir::Value, 2> shiftOps{Ops[1], Ops[2]};
    mlir::Value shifted = emitNeonCallCached(
        *this, {vTy, vTy}, shiftOps,
        usgn ? "arm.neon.vrshiftu" : "arm.neon.vrshifts", vTy, loc,
        /*isConstrainedFPIntrinsic=*/false, /*shift=*/1, /*rightshift=*/true);
    return builder.createAdd(loc, Ops[0], shifted);
  }

  // Saturating doubling long multiply: narrow inputs, wide result.
  case NEON::BI__builtin_neon_vqdmull_v: {
    cir::VectorType argTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /*truncate*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    return emitNeonCallCached(*this, {argTy, argTy}, Ops, "arm.neon.vqdmull",
                              vTy, loc);
  }
  // Saturating doubling long multiply-accumulate/subtract: vqdmull then a
  // saturating add/sub (target-independent sadd.sat/ssub.sat on arm32).
  case NEON::BI__builtin_neon_vqdmlal_v:
  case NEON::BI__builtin_neon_vqdmlsl_v: {
    llvm::SmallVector<mlir::Value, 2> mulOps(Ops.begin() + 1, Ops.end());
    cir::VectorType argTy = builder.getExtendedOrTruncatedElementVectorType(
        vTy, false /*truncate*/,
        mlir::cast<cir::IntType>(vTy.getElementType()).isSigned());
    Ops[1] = emitNeonCallCached(*this, {argTy, argTy}, mulOps,
                                "arm.neon.vqdmull", vTy, loc);
    Ops.resize(2);
    return emitNeonCallCached(*this, {vTy, vTy}, Ops,
                              BuiltinID == NEON::BI__builtin_neon_vqdmlal_v
                                  ? "sadd.sat"
                                  : "ssub.sat",
                              vTy, loc);
  }
  // Saturating (rounding) doubling multiply, returning the high half.
  case NEON::BI__builtin_neon_vqdmulh_v:
  case NEON::BI__builtin_neon_vqdmulhq_v:
    return binIntrinsic("arm.neon.vqdmulh");
  case NEON::BI__builtin_neon_vqrdmulh_v:
  case NEON::BI__builtin_neon_vqrdmulhq_v:
    return binIntrinsic("arm.neon.vqrdmulh");

  // Reciprocal / reciprocal-square-root estimate (one operand) and the
  // Newton-Raphson step (two operands). Both float and unsigned-int estimates
  // map to the single overloaded arm.neon.vrecpe/vrsqrte intrinsic.
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v:
    return emitNeonCallCached(*this, {vTy}, Ops, "arm.neon.vrecpe", vTy, loc);
  case NEON::BI__builtin_neon_vrsqrte_v:
  case NEON::BI__builtin_neon_vrsqrteq_v:
    return emitNeonCallCached(*this, {vTy}, Ops, "arm.neon.vrsqrte", vTy, loc);
  case NEON::BI__builtin_neon_vrecps_v:
  case NEON::BI__builtin_neon_vrecpsq_v:
    return binIntrinsic("arm.neon.vrecps");
  case NEON::BI__builtin_neon_vrsqrts_v:
  case NEON::BI__builtin_neon_vrsqrtsq_v:
    return binIntrinsic("arm.neon.vrsqrts");

  // Fixed-point <-> floating-point conversion with an immediate fraction-bit
  // count. The second operand is a plain i32 immediate (NOT a shift vector), so
  // only the vector operand is listed in argTypes.
  case NEON::BI__builtin_neon_vcvt_n_f32_v:
  case NEON::BI__builtin_neon_vcvtq_n_f32_v: {
    auto intEltTy = mlir::cast<cir::IntType>(vTy.getElementType());
    mlir::Type floatEleTy = intEltTy.getWidth() == 64
                                ? mlir::Type{cir::DoubleType::get(
                                      builder.getContext())}
                                : mlir::Type{cir::SingleType::get(
                                      builder.getContext())};
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    return emitNeonCallCached(
        *this, {vTy}, Ops,
        usgn ? "arm.neon.vcvtfxu2fp" : "arm.neon.vcvtfxs2fp", floatVecTy, loc);
  }
  case NEON::BI__builtin_neon_vcvt_n_s32_v:
  case NEON::BI__builtin_neon_vcvtq_n_s32_v:
  case NEON::BI__builtin_neon_vcvt_n_u32_v:
  case NEON::BI__builtin_neon_vcvtq_n_u32_v: {
    auto intEltTy = mlir::cast<cir::IntType>(vTy.getElementType());
    mlir::Type floatEleTy = intEltTy.getWidth() == 64
                                ? mlir::Type{cir::DoubleType::get(
                                      builder.getContext())}
                                : mlir::Type{cir::SingleType::get(
                                      builder.getContext())};
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    return emitNeonCallCached(
        *this, {floatVecTy}, Ops,
        usgn ? "arm.neon.vcvtfp2fxu" : "arm.neon.vcvtfp2fxs", vTy, loc);
  }

  // Integer <-> floating-point vector conversion (no fraction-bit immediate).
  // Classic CodeGen lowers these to plain sitofp/uitofp/fptosi/fptoui, so use
  // CIR int_to_float/float_to_int casts (whose signed/unsigned flavor is
  // derived from the integer operand's signedness) rather than the fixed-point
  // vcvtfx* intrinsics. int->float is 32-bit only, matching classic's AArch32
  // surface; float->int derives the source float element width from the result
  // integer element width (i16->f16, i32->f32, i64->f64), mirroring classic's
  // GetFloatNeonType.
  case NEON::BI__builtin_neon_vcvt_f32_v:
  case NEON::BI__builtin_neon_vcvtq_f32_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    auto floatVecTy = cir::VectorType::get(
        cir::SingleType::get(builder.getContext()), vTy.getSize());
    return builder.createCast(cir::CastKind::int_to_float, Ops[0], floatVecTy);
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    auto intEltTy = mlir::cast<cir::IntType>(vTy.getElementType());
    mlir::Type floatEleTy =
        intEltTy.getWidth() == 16
            ? mlir::Type{cir::FP16Type::get(builder.getContext())}
        : intEltTy.getWidth() == 64
            ? mlir::Type{cir::DoubleType::get(builder.getContext())}
            : mlir::Type{cir::SingleType::get(builder.getContext())};
    auto floatVecTy = cir::VectorType::get(floatEleTy, vTy.getSize());
    Ops[0] = builder.createBitcast(Ops[0], floatVecTy);
    return builder.createCast(cir::CastKind::float_to_int, Ops[0], vTy);
  }

  // Fused multiply-accumulate. The NEON builtin passes the accumulator first
  // (vfma(acc, m1, m2)) while the target-independent llvm.fma intrinsic wants
  // it last (fma(m1, m2, acc)), so reorder to match classic CodeGen. vfms is
  // composed in arm_neon.h as vfma(acc, -m1, m2) (a header-emitted fneg on the
  // multiply operand), so it reaches here as a plain vfma and needs nothing
  // extra.
  case NEON::BI__builtin_neon_vfma_v:
  case NEON::BI__builtin_neon_vfmaq_v: {
    llvm::SmallVector<mlir::Value, 3> fmaOps{Ops[1], Ops[2], Ops[0]};
    return emitNeonCallCached(*this, {vTy, vTy, vTy}, fmaOps, "fma", vTy, loc);
  }

  // Bitwise select. On AArch32 this lowers to the @llvm.arm.neon.vbsl
  // intrinsic, NOT to the and/and-not/or expansion AArch64 uses (AArch64 has no
  // vbsl instruction). arm_neon.h bitcasts all three operands to the byte
  // vector (int8x8_t/int8x16_t) and the builtin's return type is that byte
  // vector, so classic emits the .v8i8/.v16i8 overload with no extra bitcasts;
  // mirror that by overloading on the byte type rather than the element type.
  case NEON::BI__builtin_neon_vbsl_v:
  case NEON::BI__builtin_neon_vbslq_v: {
    cir::VectorType byteTy =
        cir::VectorType::get(builder.getSIntNTy(8), Type.isQuad() ? 16 : 8);
    return emitNeonCallCached(*this, {byteTy, byteTy, byteTy}, Ops,
                              "arm.neon.vbsl", byteTy, loc);
  }

  // Extract/concatenate: a shufflevector of the two inputs whose mask starts at
  // the immediate lane index. Same mask construction as classic CodeGen.
  case NEON::BI__builtin_neon_vext_v:
  case NEON::BI__builtin_neon_vextq_v: {
    int cv = getIntValueFromConstOp(Ops[2]);
    llvm::SmallVector<int64_t, 16> indices;
    for (unsigned i = 0, e = vTy.getSize(); i != e; ++i)
      indices.push_back(i + cv);
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    return builder.createVecShuffle(loc, Ops[0], Ops[1], indices);
  }

  // Transpose / unzip / zip: each writes a two-vector aggregate through the
  // return-slot pointer (Ops[0]) as two shufflevectors with per-operation
  // masks, exactly matching classic CodeGen. On AArch32 a stored NEON vector is
  // at most 8-byte (doubleword) aligned per the target datalayout's v128:64
  // rule; CIR's type layout doesn't model that cap, so clamp the store to 8.
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v:
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v:
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    Ops[2] = builder.createBitcast(Ops[2], vTy);
    mlir::Value baseAddr =
        builder.createBitcast(Ops[0], builder.getPointerTo(vTy));
    uint64_t abiAlign = cgm.getDataLayout().getABITypeAlign(vTy).value();
    if (abiAlign > 8)
      abiAlign = 8;
    clang::CharUnits align = clang::CharUnits::fromQuantity(abiAlign);
    unsigned numElts = vTy.getSize();
    mlir::Value sv;
    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int64_t, 16> indices;
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vtrn_v:
      case NEON::BI__builtin_neon_vtrnq_v:
        for (unsigned i = 0; i != numElts; i += 2) {
          indices.push_back(i + vi);
          indices.push_back(i + numElts + vi);
        }
        break;
      case NEON::BI__builtin_neon_vuzp_v:
      case NEON::BI__builtin_neon_vuzpq_v:
        for (unsigned i = 0; i != numElts; ++i)
          indices.push_back(2 * i + vi);
        break;
      default: // vzip
        for (unsigned i = 0; i != numElts; i += 2) {
          indices.push_back((i + vi * numElts) >> 1);
          indices.push_back(((i + vi * numElts) >> 1) + numElts);
        }
        break;
      }
      cir::ConstantOp idx = builder.getConstInt(loc, builder.getSInt32Ty(), vi);
      mlir::Value addr = cir::PtrStrideOp::create(
          builder, loc, baseAddr.getType(), baseAddr, idx,
          cir::GEPNoWrapFlags::inbounds);
      sv = builder.createVecShuffle(loc, Ops[1], Ops[2], indices);
      (void)builder.createAlignedStore(loc, sv, addr, align);
    }
    return sv;
  }

  // Broadcast one lane of a vector across every lane of the result. This is the
  // genuine builtin that arm_neon.h expands the lane-indexed multiply family
  // (vmul_lane / vmla_lane / vmls_lane / vmull_lane / vqdmull_lane /
  // vqdmulh_lane / vqrdmulh_lane and their _n siblings) into, so implementing it
  // unblocks the whole family. The result lane count differs from the source's
  // for the mixed 64/128-bit forms: splatq_lane doubles it, splat_laneq halves
  // it. Same shuffle-mask construction as classic CodeGen's EmitNeonSplat.
  case NEON::BI__builtin_neon_splat_lane_v:
  case NEON::BI__builtin_neon_splat_laneq_v:
  case NEON::BI__builtin_neon_splatq_lane_v:
  case NEON::BI__builtin_neon_splatq_laneq_v: {
    unsigned numElements = vTy.getSize();
    if (BuiltinID == NEON::BI__builtin_neon_splatq_lane_v)
      numElements = numElements * 2;
    if (BuiltinID == NEON::BI__builtin_neon_splat_laneq_v)
      numElements = numElements / 2;
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    return emitNeonSplat(builder, loc, Ops[0], Ops[1], numElements);
  }

  // (vfma_lane / vfmaq_lane are intentionally NOT handled here: arm_neon.h gates
  // them behind __aarch64__, so they are not part of the AArch32 NEON surface
  // and classic CodeGen's AArch32 path rejects them. Leaving them to fall
  // through keeps clangir's loud NYI in lockstep with classic. The _n scalar
  // forms (vfma_n etc.) instead expand to the already-supported vfma_v.)

  // Bitwise test: (a & b) != 0 as a per-lane all-ones/all-zeros mask. The
  // VecCmpOp result sign-extends the i1 comparison to the element width during
  // lowering, matching classic's and/icmp-ne/sext sequence.
  case NEON::BI__builtin_neon_vtst_v:
  case NEON::BI__builtin_neon_vtstq_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    Ops[0] = builder.createAnd(loc, Ops[0], Ops[1]);
    return cir::VecCmpOp::create(builder, loc, vTy, cir::CmpOpKind::ne, Ops[0],
                                 builder.getZero(loc, vTy));
  }

  // Floating-point absolute compare (|a| >= |b| etc). On AArch32 these lower to
  // the @llvm.arm.neon.vacge/vacgt intrinsics whose result is the integer mask
  // vector and whose two operands are the float vectors; vcale/vcalt are the
  // ge/gt forms with the operands swapped.
  case NEON::BI__builtin_neon_vcage_v:
  case NEON::BI__builtin_neon_vcageq_v:
  case NEON::BI__builtin_neon_vcagt_v:
  case NEON::BI__builtin_neon_vcagtq_v:
  case NEON::BI__builtin_neon_vcale_v:
  case NEON::BI__builtin_neon_vcaleq_v:
  case NEON::BI__builtin_neon_vcalt_v:
  case NEON::BI__builtin_neon_vcaltq_v: {
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vcage_v:
    case NEON::BI__builtin_neon_vcageq_v:
    case NEON::BI__builtin_neon_vcale_v:
    case NEON::BI__builtin_neon_vcaleq_v:
      name = "arm.neon.vacge";
      break;
    default:
      name = "arm.neon.vacgt";
      break;
    }
    if (BuiltinID == NEON::BI__builtin_neon_vcale_v ||
        BuiltinID == NEON::BI__builtin_neon_vcaleq_v ||
        BuiltinID == NEON::BI__builtin_neon_vcalt_v ||
        BuiltinID == NEON::BI__builtin_neon_vcaltq_v)
      std::swap(Ops[0], Ops[1]);
    unsigned eltBits =
        mlir::cast<cir::IntType>(vTy.getElementType()).getWidth();
    mlir::Type floatEltTy =
        eltBits == 64
            ? mlir::Type{cir::DoubleType::get(builder.getContext())}
        : eltBits == 16
            ? mlir::Type{cir::FP16Type::get(builder.getContext())}
            : mlir::Type{cir::SingleType::get(builder.getContext())};
    auto floatVecTy = cir::VectorType::get(floatEltTy, vTy.getSize());
    return emitNeonCallCached(*this, {floatVecTy, floatVecTy}, Ops, name, vTy,
                              loc);
  }

  // Table lookup / extended table lookup. On AArch32 the table is passed as
  // SEPARATE d-register (int8x8_t) arguments (already split by the frontend),
  // not as one wide vector like AArch64, so the arm.neon.vtbl{1,2,3,4} /
  // vtbx{1,2,3,4} intrinsics take N table vectors plus the index vector (vtbx
  // also prepends the passthrough accumulator). All operands and the result are
  // the byte vector <8 x i8>; the intrinsics are non-overloaded, so no type
  // suffix is mangled. Ops already holds every vector operand (the trailing
  // NeonTypeFlags immediate was dropped by the gather loop), matching classic
  // CodeGen's EmitNeonCall(getIntrinsic(vtblN/vtbxN), Ops).
  case NEON::BI__builtin_neon_vtbl1_v:
  case NEON::BI__builtin_neon_vtbl2_v:
  case NEON::BI__builtin_neon_vtbl3_v:
  case NEON::BI__builtin_neon_vtbl4_v:
  case NEON::BI__builtin_neon_vtbx1_v:
  case NEON::BI__builtin_neon_vtbx2_v:
  case NEON::BI__builtin_neon_vtbx3_v:
  case NEON::BI__builtin_neon_vtbx4_v: {
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vtbl1_v:
      name = "arm.neon.vtbl1";
      break;
    case NEON::BI__builtin_neon_vtbl2_v:
      name = "arm.neon.vtbl2";
      break;
    case NEON::BI__builtin_neon_vtbl3_v:
      name = "arm.neon.vtbl3";
      break;
    case NEON::BI__builtin_neon_vtbl4_v:
      name = "arm.neon.vtbl4";
      break;
    case NEON::BI__builtin_neon_vtbx1_v:
      name = "arm.neon.vtbx1";
      break;
    case NEON::BI__builtin_neon_vtbx2_v:
      name = "arm.neon.vtbx2";
      break;
    case NEON::BI__builtin_neon_vtbx3_v:
      name = "arm.neon.vtbx3";
      break;
    default:
      name = "arm.neon.vtbx4";
      break;
    }
    llvm::SmallVector<mlir::Type> argTypes(Ops.size(), vTy);
    return emitNeonCallCached(*this, std::move(argTypes), Ops, name, vTy, loc);
  }

  // Non-interleaved multi-vector contiguous load. arm.neon.vld1x{2,3,4} take
  // just the source pointer (no alignment operand, unlike vld{2,3,4}) and
  // RETURN an aggregate of {2,3,4} vectors; that aggregate is stored through the
  // return-slot pointer (Ops[0]) exactly as classic CodeGen does.
  case NEON::BI__builtin_neon_vld1_x2_v:
  case NEON::BI__builtin_neon_vld1q_x2_v:
  case NEON::BI__builtin_neon_vld1_x3_v:
  case NEON::BI__builtin_neon_vld1q_x3_v:
  case NEON::BI__builtin_neon_vld1_x4_v:
  case NEON::BI__builtin_neon_vld1q_x4_v: {
    unsigned numVecs;
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vld1_x2_v:
    case NEON::BI__builtin_neon_vld1q_x2_v:
      numVecs = 2;
      name = "arm.neon.vld1x2";
      break;
    case NEON::BI__builtin_neon_vld1_x3_v:
    case NEON::BI__builtin_neon_vld1q_x3_v:
      numVecs = 3;
      name = "arm.neon.vld1x3";
      break;
    default:
      numVecs = 4;
      name = "arm.neon.vld1x4";
      break;
    }
    llvm::SmallVector<mlir::Type, 4> members(numVecs, vTy);
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);
    llvm::SmallVector<mlir::Type> ldArgTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> ldArgs = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(ldArgTypes), ldArgs,
                                            name, structTy, loc);
    auto ptrTy = cir::PointerType::get(builder.getContext(), structTy);
    mlir::Value storePtr = builder.createBitcast(Ops[0], ptrTy);
    // Match classic CreateDefaultAlignedStore. On AArch32 a NEON vector (and
    // thus the multi-vector aggregate) is at most 8-byte (doubleword) aligned
    // per the target datalayout's v128:64 rule; CIR's type layout doesn't model
    // that cap, so clamp the aggregate ABI alignment to 8.
    uint64_t abiAlign = cgm.getDataLayout().getABITypeAlign(structTy).value();
    if (abiAlign > 8)
      abiAlign = 8;
    (void)builder.createAlignedStore(
        loc, result, storePtr, clang::CharUnits::fromQuantity(abiAlign));
    return result;
  }

  // Non-interleaved multi-vector contiguous store. arm.neon.vst1x{2,3,4} take
  // the destination pointer followed by the {2,3,4} source vectors (already
  // split into separate arguments by the frontend) and no alignment operand.
  // The intrinsic overloads its pointer operand first, so the pointer stays at
  // Ops[0].
  case NEON::BI__builtin_neon_vst1_x2_v:
  case NEON::BI__builtin_neon_vst1q_x2_v:
  case NEON::BI__builtin_neon_vst1_x3_v:
  case NEON::BI__builtin_neon_vst1q_x3_v:
  case NEON::BI__builtin_neon_vst1_x4_v:
  case NEON::BI__builtin_neon_vst1q_x4_v: {
    unsigned numVecs;
    llvm::StringRef name;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vst1_x2_v:
    case NEON::BI__builtin_neon_vst1q_x2_v:
      numVecs = 2;
      name = "arm.neon.vst1x2";
      break;
    case NEON::BI__builtin_neon_vst1_x3_v:
    case NEON::BI__builtin_neon_vst1q_x3_v:
      numVecs = 3;
      name = "arm.neon.vst1x3";
      break;
    default:
      numVecs = 4;
      name = "arm.neon.vst1x4";
      break;
    }
    // Ops layout: [dstPtr, vec0..vec{N-1}].
    llvm::SmallVector<mlir::Type> stArgTypes = {Ops[0].getType()};
    llvm::SmallVector<mlir::Value> stArgs = {Ops[0]};
    for (unsigned v = 0; v < numVecs; ++v) {
      stArgTypes.push_back(vTy);
      stArgs.push_back(builder.createBitcast(Ops[1 + v], vTy));
    }
    return emitNeonCallCached(*this, std::move(stArgTypes), stArgs, name,
                              builder.getVoidTy(), loc);
  }
  }
}

std::optional<mlir::Value>
CIRGenFunction::emitAArch64BuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                       ReturnValueSlot ReturnValue,
                                       llvm::Triple::ArchType Arch) {
  if (BuiltinID >= clang::AArch64::FirstSVEBuiltin &&
      BuiltinID <= clang::AArch64::LastSVEBuiltin)
    return emitAArch64SVEBuiltinExpr(BuiltinID, E);

  if (BuiltinID >= clang::AArch64::FirstSMEBuiltin &&
      BuiltinID <= clang::AArch64::LastSMEBuiltin)
    return emitAArch64SMEBuiltinExpr(BuiltinID, E);

  if (BuiltinID == Builtin::BI__builtin_cpu_supports) {
    mlir::Location loc = getLoc(E->getSourceRange());
    const Expr *featureExpr = E->getArg(0)->IgnoreParenCasts();
    StringRef featureStr = cast<clang::StringLiteral>(featureExpr)->getString();

    llvm::SmallVector<StringRef, 8> splitFeatures;
    featureStr.split(splitFeatures, '+');
    llvm::SmallVector<StringRef, 8> features;
    for (StringRef feature : splitFeatures) {
      feature = feature.trim();
      if (!llvm::AArch64::parseFMVExtension(feature))
        return builder.getBool(false, loc);
      if (feature != "default")
        features.push_back(feature);
    }

    auto boolTy = cir::BoolType::get(builder.getContext());
    auto uint64Ty = cir::IntType::get(builder.getContext(), 64, false);
    auto cpuFeaturesRecTy = cir::RecordType::get(
        builder.getContext(), {uint64Ty}, /*packed=*/false,
        /*padded=*/false, cir::RecordType::Struct);
    getOrCreateExternGlobal(cgm, loc, "__aarch64_cpu_features",
                            cpuFeaturesRecTy);

    mlir::Value result = builder.getBool(true, loc);
    llvm::APInt featureMask = llvm::AArch64::getCpuSupportsMask(features);
    if (featureMask != 0) {
      auto modelAddr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuFeaturesRecTy),
          "__aarch64_cpu_features");
      auto featuresPtr = cir::GetMemberOp::create(
          builder, loc, cir::PointerType::get(uint64Ty), modelAddr,
          /*name=*/"", /*index=*/0);
      auto align8 = mlir::IntegerAttr::get(
          mlir::IntegerType::get(builder.getContext(), 64), 8);
      mlir::Value featuresVal = cir::LoadOp::create(
          builder, loc, featuresPtr, /*isDeref=*/false,
          /*isVolatile=*/false, /*is_nontemporal=*/false,
          /*alignment=*/align8, /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
      auto maskVal = cir::ConstantOp::create(
          builder, loc, uint64Ty,
          builder.getUInt64AttrLocalCached(featureMask.getZExtValue()));
      mlir::Value andResult = cir::BinOp::create(
          builder, loc, uint64Ty, cir::BinOpKind::And, featuresVal, maskVal,
          {}, {}, {}, {}, {}, {});
      mlir::Value cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                           andResult, maskVal);
      result = cir::BinOp::create(builder, loc, boolTy, cir::BinOpKind::And,
                                  result, cmp, {}, {}, {}, {}, {}, {});
    }

    return result;
  }

  unsigned HintID = static_cast<unsigned>(-1);
  switch (BuiltinID) {
  default:
    break;
  case clang::AArch64::BI__builtin_arm_nop:
    HintID = 0;
    break;
  case clang::AArch64::BI__builtin_arm_yield:
  case clang::AArch64::BI__yield:
    HintID = 1;
    break;
  case clang::AArch64::BI__builtin_arm_wfe:
  case clang::AArch64::BI__wfe:
    HintID = 2;
    break;
  case clang::AArch64::BI__builtin_arm_wfi:
  case clang::AArch64::BI__wfi:
    HintID = 3;
    break;
  case clang::AArch64::BI__builtin_arm_sev:
  case clang::AArch64::BI__sev:
    HintID = 4;
    break;
  case clang::AArch64::BI__builtin_arm_sevl:
  case clang::AArch64::BI__sevl:
    HintID = 5;
    break;
  }

  if (HintID != static_cast<unsigned>(-1)) {
    mlir::Location loc = getLoc(E->getSourceRange());
    mlir::Value hintValue =
        builder.getConstInt(loc, builder.getSInt32Ty(), HintID);
    cir::LLVMIntrinsicCallOp::create(builder, loc,
                                     cgm.getStringAttrCached("aarch64.hint"),
                                     mlir::Type{}, hintValue);
    return nullptr;
  }

  // Memory barrier builtins: emit llvm.aarch64.dmb/dsb/isb intrinsics.
  if (BuiltinID == clang::AArch64::BI__builtin_arm_dmb ||
      BuiltinID == clang::AArch64::BI__builtin_arm_dsb ||
      BuiltinID == clang::AArch64::BI__builtin_arm_isb) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    arg = builder.createIntCast(arg, builder.getSInt32Ty());
    StringRef intrinsicName =
        BuiltinID == clang::AArch64::BI__builtin_arm_dmb ? "aarch64.dmb"
        : BuiltinID == clang::AArch64::BI__builtin_arm_dsb
            ? "aarch64.dsb"
            : "aarch64.isb";
    cir::LLVMIntrinsicCallOp::create(builder, loc,
                                     cgm.getStringAttrCached(intrinsicName),
                                     mlir::Type{}, mlir::ValueRange{arg});
    return nullptr;
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_trap) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    arg = builder.createIntCast(arg, builder.getUInt32Ty());
    cir::LLVMIntrinsicCallOp::create(builder, loc,
                                     cgm.getStringAttrCached("aarch64.break"),
                                     mlir::Type{}, mlir::ValueRange{arg});
    return nullptr;
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_get_sme_state) {
    // Create call to __arm_sme_state and store the results to the two pointers.
    llvm_unreachable("clang::AArch64::BI__builtin_arm_get_sme_state NYI");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rbit) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "rbit of unusual size!");
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return cir::BitReverseOp::create(builder, getLoc(E->getExprLoc()), arg);
  }
  if (BuiltinID == clang::AArch64::BI__builtin_arm_rbit64) {
    assert((getContext().getTypeSize(E->getType()) == 64) &&
           "rbit of unusual size!");
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return cir::BitReverseOp::create(builder, getLoc(E->getExprLoc()), arg);
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_clz ||
      BuiltinID == clang::AArch64::BI__builtin_arm_clz64) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    mlir::Value isZeroPoison = builder.getFalse(loc);
    mlir::Value res =
        emitIntrinsicCallOpCached(cgm, builder, loc, "ctlz", arg.getType(),
                                  mlir::ValueRange{arg, isZeroPoison});
    if (BuiltinID == clang::AArch64::BI__builtin_arm_clz64)
      res = builder.createIntCast(res, mlir::cast<cir::IntType>(
                                           convertType(E->getType())));
    return res;
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_cls) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.cls",
                                     convertType(E->getType()),
                                     mlir::ValueRange{arg});
  }
  if (BuiltinID == clang::AArch64::BI__builtin_arm_cls64) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.cls64",
                                     convertType(E->getType()),
                                     mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint32zf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint32z) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.frint32z",
                                     arg.getType(), mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint64zf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint64z) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.frint64z",
                                     arg.getType(), mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint32xf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint32x) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.frint32x",
                                     arg.getType(), mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint64xf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint64x) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.frint64x",
                                     arg.getType(), mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_jcvt) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "__jcvt of unusual size!");
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.fjcvtzs",
                                     builder.getSInt32Ty(),
                                     mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_ld64b ||
      BuiltinID == clang::AArch64::BI__builtin_arm_st64b ||
      BuiltinID == clang::AArch64::BI__builtin_arm_st64bv ||
      BuiltinID == clang::AArch64::BI__builtin_arm_st64bv0) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value memAddr = emitScalarExpr(E->getArg(0));
    mlir::Value valPtr = emitScalarExpr(E->getArg(1));

    cir::IntType i64Ty = builder.getUIntNTy(64);
    cir::IntType sInt32Ty = builder.getSIntNTy(32);

    // Cast ValPtr to !cir.ptr<i64> for typed loads/stores.
    mlir::Value typedValPtr = builder.createPtrBitcast(valPtr, i64Ty);

    if (BuiltinID == clang::AArch64::BI__builtin_arm_ld64b) {
      // Load from the address via an LLVM intrinsic, receiving a
      // tuple of 8 i64 words, and store each one to ValPtr.
      llvm::SmallVector<mlir::Type, 8> memberTypes(8, i64Ty);
      cir::RecordType retTy = builder.getAnonRecordTy(memberTypes);
      mlir::Value result =
          emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.ld64b", retTy,
                                    mlir::ValueRange{memAddr});
      for (unsigned i = 0; i < 8; i++) {
        mlir::Value val =
            cir::ExtractMemberOp::create(builder, loc, result, i);
        cir::ConstantOp idx = builder.getConstInt(loc, sInt32Ty, i);
        mlir::Value offsetPtr = builder.createPtrStride(
            loc, typedValPtr, idx, cir::GEPNoWrapFlags::none);
        cir::StoreOp::create(builder, loc, val, offsetPtr,
                             /*isVolatile=*/false, /*is_nontemporal=*/false,
                             builder.getAlignmentAttr(8), /*sync_scope=*/{},
                             /*mem_order=*/{}, /*tbaa=*/{});
      }
      return nullptr;
    } else {
      // Load 8 i64 words from ValPtr, and store them to the address
      // via an LLVM intrinsic.
      llvm::SmallVector<mlir::Value, 9> args;
      args.push_back(memAddr);
      for (unsigned i = 0; i < 8; i++) {
        cir::ConstantOp idx = builder.getConstInt(loc, sInt32Ty, i);
        mlir::Value offsetPtr = builder.createPtrStride(
            loc, typedValPtr, idx, cir::GEPNoWrapFlags::none);
        Address addr(offsetPtr, i64Ty, clang::CharUnits::fromQuantity(8));
        args.push_back(builder.createLoad(loc, addr));
      }

      if (BuiltinID == clang::AArch64::BI__builtin_arm_st64b) {
        cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached("aarch64.st64b"),
            mlir::Type{}, mlir::ValueRange{args});
        return nullptr;
      }
      auto intrName = (BuiltinID == clang::AArch64::BI__builtin_arm_st64bv)
                           ? "aarch64.st64bv"
                           : "aarch64.st64bv0";
      return emitIntrinsicCallOpCached(cgm, builder, loc, intrName, i64Ty,
                                       mlir::ValueRange{args});
    }
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rndr ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rndrrs) {
    mlir::Location loc = getLoc(E->getExprLoc());

    auto intrName = (BuiltinID == clang::AArch64::BI__builtin_arm_rndr)
                        ? "aarch64.rndr"
                        : "aarch64.rndrrs";

    // The intrinsic returns {i64, i1}. Create a matching CIR record type.
    cir::IntType i64Ty = builder.getUIntNTy(64);
    cir::IntType i1Ty = builder.getUIntNTy(1);
    cir::RecordType retTy = builder.getAnonRecordTy({i64Ty, i1Ty});

    mlir::Value result = emitIntrinsicCallOpCached(
        cgm, builder, loc, intrName, retTy, mlir::ValueRange{});

    // Extract the random value (element 0) and store to the output pointer.
    mlir::Value randomValue =
        cir::ExtractMemberOp::create(builder, loc, result, /*index=*/0);
    Address memAddr = emitPointerWithAlignment(E->getArg(0));
    builder.createStore(loc, randomValue, memAddr);

    // Extract the status bit (element 1) and zero-extend to i32.
    mlir::Value status =
        cir::ExtractMemberOp::create(builder, loc, result, /*index=*/1);
    return builder.createIntCast(status, builder.getSIntNTy(32));
  }

  if (BuiltinID == clang::AArch64::BI__clear_cache) {
    assert(E->getNumArgs() == 2 && "__clear_cache takes 2 arguments");
    mlir::Location loc = getLoc(E->getExprLoc());
    const FunctionDecl *FD = E->getDirectCallee();
    mlir::Value ops[] = {emitScalarExpr(E->getArg(0)),
                         emitScalarExpr(E->getArg(1))};
    // Build the CIR function type matching the C function type.
    auto ptrTy = builder.getVoidPtrTy();
    auto voidTy = cir::VoidType::get(builder.getContext());
    auto fnTy = cir::FuncType::get({ptrTy, ptrTy}, voidTy);
    cir::FuncOp fn = cgm.createRuntimeFunction(fnTy, FD->getName());
    emitNounwindRuntimeCall(loc, fn, ops);
    return nullptr;
  }

  if ((BuiltinID == clang::AArch64::BI__builtin_arm_ldrex ||
       BuiltinID == clang::AArch64::BI__builtin_arm_ldaex) &&
      getContext().getTypeSize(E->getType()) == 128) {
    return emitArmLdrex128Intrinsic(BuiltinID, E, *this);
  } else if (BuiltinID == clang::AArch64::BI__builtin_arm_ldrex ||
             BuiltinID == clang::AArch64::BI__builtin_arm_ldaex) {
    return emitArmLdrexNon128Intrinsic(BuiltinID, E, *this);
  }

  if ((BuiltinID == clang::AArch64::BI__builtin_arm_strex ||
       BuiltinID == clang::AArch64::BI__builtin_arm_stlex) &&
      getContext().getTypeSize(E->getArg(0)->getType()) == 128) {
    return emitArmStrex128Intrinsic(BuiltinID, E, *this);
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_strex ||
      BuiltinID == clang::AArch64::BI__builtin_arm_stlex) {
    return emitArmStrexNon128Intrinsic(BuiltinID, E, *this);
  }

  if (BuiltinID == clang::AArch64::BI__getReg) {
    mlir::Location loc = getLoc(E->getExprLoc());
    Expr::EvalResult result;
    if (!E->getArg(0)->EvaluateAsInt(result, cgm.getASTContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::APSInt value = result.Val.getInt();
    std::string reg = value == 31 ? "sp" : "x" + llvm::toString(value, 10);

    cir::IntType uint64Ty = builder.getUIntNTy(64);
    return cir::ReadRegisterOp::create(builder, loc, uint64Ty, reg);
  }

  if (BuiltinID == clang::AArch64::BI__break) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.break",
                                     builder.getVoidTy(),
                                     mlir::ValueRange{arg});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_clrex) {
    // Maps to @llvm.aarch64.clrex — clears the exclusive monitor.
    cir::LLVMIntrinsicCallOp::create(
        builder, getLoc(E->getSourceRange()),
        cgm.getStringAttrCached("aarch64.clrex"),
        mlir::Type{}, mlir::ValueRange{});
    return nullptr;
  }

  if (BuiltinID == clang::AArch64::BI_ReadWriteBarrier) {
    // Compiler fence — single-thread sequentially consistent fence.
    cir::AtomicFenceOp::create(
        builder, getLoc(E->getExprLoc()),
        cir::MemOrder::SequentiallyConsistent,
        cir::SyncScopeKindAttr::get(&getMLIRContext(),
                                    cir::SyncScopeKind::SingleThread));
    return nullptr;
  }

  // CRC32
  StringRef crcIntrinsicName;
  switch (BuiltinID) {
  case clang::AArch64::BI__builtin_arm_crc32b:
    crcIntrinsicName = "aarch64.crc32b";
    break;
  case clang::AArch64::BI__builtin_arm_crc32cb:
    crcIntrinsicName = "aarch64.crc32cb";
    break;
  case clang::AArch64::BI__builtin_arm_crc32h:
    crcIntrinsicName = "aarch64.crc32h";
    break;
  case clang::AArch64::BI__builtin_arm_crc32ch:
    crcIntrinsicName = "aarch64.crc32ch";
    break;
  case clang::AArch64::BI__builtin_arm_crc32w:
    crcIntrinsicName = "aarch64.crc32w";
    break;
  case clang::AArch64::BI__builtin_arm_crc32cw:
    crcIntrinsicName = "aarch64.crc32cw";
    break;
  case clang::AArch64::BI__builtin_arm_crc32d:
    crcIntrinsicName = "aarch64.crc32x";
    break;
  case clang::AArch64::BI__builtin_arm_crc32cd:
    crcIntrinsicName = "aarch64.crc32cx";
    break;
  }

  if (!crcIntrinsicName.empty()) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg0 = emitScalarExpr(E->getArg(0));
    mlir::Value arg1 = emitScalarExpr(E->getArg(1));

    // The intrinsic data parameter is i32 for b/h/w variants and i64 for
    // d variants (crc32x/crc32cx). ZExt arg1 to match.
    bool is64BitData =
        (BuiltinID == clang::AArch64::BI__builtin_arm_crc32d ||
         BuiltinID == clang::AArch64::BI__builtin_arm_crc32cd);
    cir::IntType dataTy = is64BitData ? builder.getUInt64Ty()
                                      : builder.getUInt32Ty();
    arg1 = builder.createIntCast(arg1, dataTy);

    cir::IntType resTy = builder.getUInt32Ty();
    return emitIntrinsicCallOpCached(cgm, builder, loc, crcIntrinsicName, resTy,
                                     mlir::ValueRange{arg0, arg1});
  }

  // Memory Operations (MOPS)
  if (BuiltinID == AArch64::BI__builtin_arm_mops_memset_tag) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value dst = emitScalarExpr(E->getArg(0));
    mlir::Value val = emitScalarExpr(E->getArg(1));
    mlir::Value size = emitScalarExpr(E->getArg(2));
    val = builder.createIntCast(val, builder.getUInt8Ty());
    size = builder.createIntCast(size, builder.getUInt64Ty());
    return emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "aarch64.mops.memset.tag",
                                     dst.getType(),
                                     mlir::ValueRange{dst, val, size});
  }

  // Memory Tagging Extensions (MTE) Intrinsics
  switch (BuiltinID) {
  case clang::AArch64::BI__builtin_arm_irg: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value pointer = emitScalarExpr(E->getArg(0));
    mlir::Value mask = emitScalarExpr(E->getArg(1));
    mask = builder.createIntCast(mask, builder.getUInt64Ty());
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.irg",
                                     pointer.getType(),
                                     mlir::ValueRange{pointer, mask});
  }
  case clang::AArch64::BI__builtin_arm_addg: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value pointer = emitScalarExpr(E->getArg(0));
    mlir::Value tagOffset = emitScalarExpr(E->getArg(1));
    tagOffset = builder.createIntCast(tagOffset, builder.getUInt64Ty());
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.addg",
                                     pointer.getType(),
                                     mlir::ValueRange{pointer, tagOffset});
  }
  case clang::AArch64::BI__builtin_arm_gmi: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value pointer = emitScalarExpr(E->getArg(0));
    mlir::Value excludedMask = emitScalarExpr(E->getArg(1));
    excludedMask = builder.createIntCast(excludedMask, builder.getUInt64Ty());
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.gmi",
                                     builder.getUInt64Ty(),
                                     mlir::ValueRange{pointer, excludedMask});
  }
  case clang::AArch64::BI__builtin_arm_ldg: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value tagAddress = emitScalarExpr(E->getArg(0));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.ldg",
                                     tagAddress.getType(),
                                     mlir::ValueRange{tagAddress, tagAddress});
  }
  case clang::AArch64::BI__builtin_arm_stg: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value tagAddress = emitScalarExpr(E->getArg(0));
    cir::LLVMIntrinsicCallOp::create(builder, loc,
                                     cgm.getStringAttrCached("aarch64.stg"),
                                     mlir::Type{},
                                     mlir::ValueRange{tagAddress, tagAddress});
    return nullptr;
  }
  case clang::AArch64::BI__builtin_arm_subp: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value pointerA = emitScalarExpr(E->getArg(0));
    mlir::Value pointerB = emitScalarExpr(E->getArg(1));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.subp",
                                     builder.getSInt64Ty(),
                                     mlir::ValueRange{pointerA, pointerB});
  }
  default:
    break;
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rsr ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rsr128 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsr ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsr128 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsrp) {
    mlir::Location loc = getLoc(E->getExprLoc());

    bool IsRead = BuiltinID == clang::AArch64::BI__builtin_arm_rsr ||
                  BuiltinID == clang::AArch64::BI__builtin_arm_rsr64 ||
                  BuiltinID == clang::AArch64::BI__builtin_arm_rsr128 ||
                  BuiltinID == clang::AArch64::BI__builtin_arm_rsrp;

    bool IsPointerBuiltin = BuiltinID == clang::AArch64::BI__builtin_arm_rsrp ||
                            BuiltinID == clang::AArch64::BI__builtin_arm_wsrp;

    bool Is32Bit = BuiltinID == clang::AArch64::BI__builtin_arm_rsr ||
                   BuiltinID == clang::AArch64::BI__builtin_arm_wsr;

    bool Is128Bit = BuiltinID == clang::AArch64::BI__builtin_arm_rsr128 ||
                    BuiltinID == clang::AArch64::BI__builtin_arm_wsr128;

    // Get the register name from the string literal first argument.
    const Expr *sysRegStrExpr = E->getArg(0)->IgnoreParenCasts();
    StringRef sysReg = cast<clang::StringLiteral>(sysRegStrExpr)->getString();

    // Determine RegisterType and ValueType matching OGCG's AArch64 path.
    // On AArch64, RegisterType defaults to i64. For 128-bit, both are i128.
    // For 32-bit rsr/wsr, RegisterType=i64 but ValueType=i32 (mixed).
    cir::IntType registerTy = builder.getUIntNTy(64);
    if (Is128Bit)
      registerTy = builder.getUIntNTy(128);

    if (IsRead) {
      auto readOp =
          cir::ReadRegisterOp::create(builder, loc, registerTy, sysReg);
      readOp.setIsVolatile(true);
      mlir::Value result = readOp;

      bool mixedTypes = !Is128Bit && Is32Bit;
      if (mixedTypes) {
        // Read into 64-bit register, then truncate to 32-bit.
        return builder.createIntCast(result, builder.getUIntNTy(32));
      }
      if (IsPointerBuiltin) {
        // Convert integer register result to void pointer.
        return builder.createIntToPtr(result, builder.getVoidPtrTy());
      }
      return result;
    }

    // Write path.
    mlir::Value argValue = emitScalarExpr(E->getArg(1));
    bool mixedTypes = !Is128Bit && Is32Bit;
    if (mixedTypes) {
      // Extend 32-bit value to 64-bit register width.
      argValue = builder.createIntCast(argValue, registerTy);
    } else if (IsPointerBuiltin) {
      // Convert void pointer to integer register type.
      argValue = builder.createPtrToInt(argValue, registerTy);
    }
    cir::WriteRegisterOp::create(builder, loc, sysReg, argValue);
    return nullptr;
  }

  if (BuiltinID == clang::AArch64::BI__builtin_sponentry) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Type resTy = convertType(E->getType());
    return emitIntrinsicCallOpCached(cgm, builder, loc, "sponentry", resTy,
                                     mlir::ValueRange{});
  }

  if (BuiltinID == clang::AArch64::BI_ReadStatusReg ||
      BuiltinID == clang::AArch64::BI_WriteStatusReg ||
      BuiltinID == clang::AArch64::BI__sys) {
    mlir::Location loc = getLoc(E->getExprLoc());

    unsigned sysReg =
        E->getArg(0)->EvaluateKnownConstInt(getContext()).getZExtValue();

    // Construct the system register string "op0:op1:CRn:CRm:op2".
    std::string sysRegStr;
    unsigned sysRegOp0 = (BuiltinID == clang::AArch64::BI_ReadStatusReg ||
                          BuiltinID == clang::AArch64::BI_WriteStatusReg)
                             ? ((1 << 1) | ((sysReg >> 14) & 1))
                             : 1;
    llvm::raw_string_ostream(sysRegStr)
        << sysRegOp0 << ":" << ((sysReg >> 11) & 7) << ":"
        << ((sysReg >> 7) & 15) << ":" << ((sysReg >> 3) & 15) << ":"
        << (sysReg & 7);

    if (BuiltinID == clang::AArch64::BI_ReadStatusReg) {
      mlir::Type resTy = convertType(E->getType());
      return cir::ReadRegisterOp::create(builder, loc, resTy, sysRegStr);
    }

    mlir::Value argValue = emitScalarExpr(E->getArg(1));
    cir::WriteRegisterOp::create(builder, loc, sysRegStr, argValue);
    if (BuiltinID == clang::AArch64::BI__sys) {
      // Return 0 for convenience, even though MSVC returns some other
      // undefined value.
      return builder.getConstant(loc, builder.getUInt32AttrLocalCached(0));
    }
    return nullptr;
  }

  if (BuiltinID == clang::AArch64::BI_AddressOfReturnAddress) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Type resTy = convertType(E->getType());
    return emitIntrinsicCallOpCached(cgm, builder, loc,
                                     "addressofreturnaddress", resTy,
                                     mlir::ValueRange{});
  }

  if (BuiltinID == clang::AArch64::BI__mulh ||
      BuiltinID == clang::AArch64::BI__umulh) {
    mlir::Location loc = getLoc(E->getExprLoc());
    bool isSigned = (BuiltinID == clang::AArch64::BI__mulh);
    cir::IntType i128Ty = isSigned ? builder.getSIntNTy(128)
                                   : builder.getUIntNTy(128);

    mlir::Value lhs = builder.createIntCast(emitScalarExpr(E->getArg(0)),
                                            i128Ty);
    mlir::Value rhs = builder.createIntCast(emitScalarExpr(E->getArg(1)),
                                            i128Ty);

    mlir::Value mulResult = isSigned ? builder.createNSWMul(loc, lhs, rhs)
                                     : builder.createNUWAMul(loc, lhs, rhs);

    // Shift right by 64 to extract the high bits.
    mlir::Value higherBits = builder.createShiftRight(loc, mulResult, 64);

    // Truncate back to the return type (i64).
    return builder.createIntCast(
        higherBits, mlir::cast<cir::IntType>(convertType(E->getType())));
  }

  // Helper lambda: read the x18 register as an i8 pointer.
  // x18 is read via llvm.read_register.i64 with "x18" metadata, then
  // converted to an i8 pointer. This mirrors OGCG's readX18AsPtr helper.
  auto readX18AsPtr = [&]() -> mlir::Value {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::IntType uint64Ty = builder.getUIntNTy(64);
    mlir::Value x18 =
        cir::ReadRegisterOp::create(builder, loc, uint64Ty, "x18");
    return builder.createIntToPtr(x18, builder.getUInt8PtrTy());
  };

  if (BuiltinID == AArch64::BI__writex18byte ||
      BuiltinID == AArch64::BI__writex18word ||
      BuiltinID == AArch64::BI__writex18dword ||
      BuiltinID == AArch64::BI__writex18qword) {
    mlir::Location loc = getLoc(E->getExprLoc());

    // Emit args: (offset, data).
    mlir::Value offsetArg = emitScalarExpr(E->getArg(0));
    mlir::Value dataArg = emitScalarExpr(E->getArg(1));

    // Read x18 as i8*.
    mlir::Value x18Ptr = readX18AsPtr();

    // ZExt offset to i64, GEP to x18 + offset.
    mlir::Value offset =
        builder.createIntCast(offsetArg, builder.getUIntNTy(64));
    mlir::Value ptr = builder.createPtrStride(loc, x18Ptr, offset,
                                               cir::GEPNoWrapFlags::none);

    // Bitcast to typed pointer and store with 1-byte alignment.
    mlir::Type dataTy = dataArg.getType();
    mlir::Value typedPtr = builder.createPtrBitcast(ptr, dataTy);
    cir::StoreOp::create(builder, loc, dataArg, typedPtr, /*isVolatile=*/false,
                         /*is_nontemporal=*/false, builder.getAlignmentAttr(1),
                         /*sync_scope=*/{}, /*mem_order=*/{}, /*tbaa=*/{});
    return nullptr;
  }

  if (BuiltinID == AArch64::BI__readx18byte ||
      BuiltinID == AArch64::BI__readx18word ||
      BuiltinID == AArch64::BI__readx18dword ||
      BuiltinID == AArch64::BI__readx18qword) {
    mlir::Location loc = getLoc(E->getExprLoc());

    // Emit arg: (offset).
    mlir::Value offsetArg = emitScalarExpr(E->getArg(0));

    // Read x18 as i8*.
    mlir::Value x18Ptr = readX18AsPtr();

    // ZExt offset to i64, GEP to x18 + offset.
    mlir::Value offset =
        builder.createIntCast(offsetArg, builder.getUIntNTy(64));
    mlir::Value ptr = builder.createPtrStride(loc, x18Ptr, offset,
                                               cir::GEPNoWrapFlags::none);

    // Bitcast to typed pointer and load with 1-byte alignment.
    mlir::Type resultTy = convertType(E->getType());
    return builder.createAlignedLoad(loc, resultTy, ptr, llvm::MaybeAlign(1));
  }

  if (BuiltinID == AArch64::BI__addx18byte ||
      BuiltinID == AArch64::BI__addx18word ||
      BuiltinID == AArch64::BI__addx18dword ||
      BuiltinID == AArch64::BI__addx18qword ||
      BuiltinID == AArch64::BI__incx18byte ||
      BuiltinID == AArch64::BI__incx18word ||
      BuiltinID == AArch64::BI__incx18dword ||
      BuiltinID == AArch64::BI__incx18qword) {
    mlir::Location loc = getLoc(E->getExprLoc());

    // Determine the integer type and whether this is an increment (add 1).
    cir::IntType intTy;
    bool isIncrement;
    switch (BuiltinID) {
    case AArch64::BI__incx18byte:
      intTy = builder.getUIntNTy(8);
      isIncrement = true;
      break;
    case AArch64::BI__incx18word:
      intTy = builder.getUIntNTy(16);
      isIncrement = true;
      break;
    case AArch64::BI__incx18dword:
      intTy = builder.getUIntNTy(32);
      isIncrement = true;
      break;
    case AArch64::BI__incx18qword:
      intTy = builder.getUIntNTy(64);
      isIncrement = true;
      break;
    default:
      intTy = mlir::cast<cir::IntType>(convertType(E->getArg(1)->getType()));
      isIncrement = false;
      break;
    }

    // Emit args: (offset) or (offset, value).
    mlir::Value offsetArg = emitScalarExpr(E->getArg(0));
    mlir::Value valToAdd;
    if (isIncrement) {
      cir::IntAttr oneAttr;
      if (intTy == builder.getUInt8Ty())
        oneAttr = builder.getUInt8AttrLocalCached(1);
      else if (intTy == builder.getUInt16Ty())
        oneAttr = builder.getUInt16AttrLocalCached(1);
      else if (intTy == builder.getUInt32Ty())
        oneAttr = builder.getUInt32AttrLocalCached(1);
      else if (intTy == builder.getUInt64Ty())
        oneAttr = builder.getUInt64AttrLocalCached(1);
      else
        oneAttr = cir::IntAttr::get(intTy, 1);
      valToAdd = builder.getConstant(loc, oneAttr);
    } else {
      valToAdd = emitScalarExpr(E->getArg(1));
    }

    // Read x18 as i8*.
    mlir::Value x18Ptr = readX18AsPtr();

    // ZExt offset to i64, GEP to x18 + offset.
    mlir::Value offset =
        builder.createIntCast(offsetArg, builder.getUIntNTy(64));
    mlir::Value ptr = builder.createPtrStride(loc, x18Ptr, offset,
                                               cir::GEPNoWrapFlags::none);

    // Bitcast to typed pointer.
    mlir::Value typedPtr = builder.createPtrBitcast(ptr, intTy);

    // Load current value, add, store back — all with 1-byte alignment.
    mlir::Value loaded = builder.createAlignedLoad(loc, intTy, typedPtr,
                                                   llvm::MaybeAlign(1));
    mlir::Value addResult = builder.createAdd(loc, loaded, valToAdd);
    cir::StoreOp::create(builder, loc, addResult, typedPtr,
                         /*isVolatile=*/false, /*is_nontemporal=*/false,
                         builder.getAlignmentAttr(1),
                         /*sync_scope=*/{}, /*mem_order=*/{}, /*tbaa=*/{});
    return nullptr;
  }

  if (BuiltinID == AArch64::BI_CopyDoubleFromInt64 ||
      BuiltinID == AArch64::BI_CopyFloatFromInt32 ||
      BuiltinID == AArch64::BI_CopyInt32FromFloat ||
      BuiltinID == AArch64::BI_CopyInt64FromDouble) {
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    mlir::Type retTy = convertType(E->getType());
    return builder.createBitcast(arg, retTy);
  }

  if (BuiltinID == AArch64::BI_CountLeadingOnes ||
      BuiltinID == AArch64::BI_CountLeadingOnes64 ||
      BuiltinID == AArch64::BI_CountLeadingZeros ||
      BuiltinID == AArch64::BI_CountLeadingZeros64) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));

    // For CountLeadingOnes, XOR with all-ones to convert to CLZ.
    if (BuiltinID == AArch64::BI_CountLeadingOnes ||
        BuiltinID == AArch64::BI_CountLeadingOnes64) {
      mlir::Value allOnes;
      if (arg.getType() == builder.getUInt32Ty())
        allOnes = builder.getConstant(
            loc, builder.getUInt32AttrLocalCached(static_cast<uint32_t>(-1)));
      else if (arg.getType() == builder.getUInt64Ty())
        allOnes = builder.getConstant(
            loc, builder.getUInt64AttrLocalCached(static_cast<uint64_t>(-1)));
      else
        allOnes = builder.getConstant(loc, cir::IntAttr::get(arg.getType(), -1));
      arg = cir::BinOp::create(builder, loc, arg.getType(),
                                cir::BinOpKind::Xor, arg, allOnes,
                                {}, {}, {}, {}, {}, {});
    }

    mlir::Value isZeroPoison = builder.getFalse(loc);
    mlir::Value result =
        emitIntrinsicCallOpCached(cgm, builder, loc, "ctlz", arg.getType(),
                                  mlir::ValueRange{arg, isZeroPoison});

    // 64-bit variants return i32 (truncate from i64 result).
    if (BuiltinID == AArch64::BI_CountLeadingOnes64 ||
        BuiltinID == AArch64::BI_CountLeadingZeros64)
      result = builder.createIntCast(
          result, mlir::cast<cir::IntType>(convertType(E->getType())));
    return result;
  }

  if (BuiltinID == AArch64::BI_CountLeadingSigns ||
      BuiltinID == AArch64::BI_CountLeadingSigns64) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    StringRef intrinName = (BuiltinID == AArch64::BI_CountLeadingSigns)
                               ? "aarch64.cls"
                               : "aarch64.cls64";
    mlir::Value result = emitIntrinsicCallOpCached(
        cgm, builder, loc, intrinName, convertType(E->getType()),
        mlir::ValueRange{arg});
    // cls64 returns i32 natively, no truncation needed.
    return result;
  }

  if (BuiltinID == AArch64::BI_CountOneBits ||
      BuiltinID == AArch64::BI_CountOneBits64) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    mlir::Value result = emitIntrinsicCallOpCached(cgm, builder, loc, "ctpop",
                                                   arg.getType(),
                                                   mlir::ValueRange{arg});
    // 64-bit variant returns i32 (truncate from i64 result).
    if (BuiltinID == AArch64::BI_CountOneBits64)
      result = builder.createIntCast(
          result, mlir::cast<cir::IntType>(convertType(E->getType())));
    return result;
  }

  if (BuiltinID == AArch64::BI__prefetch) {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value address = emitScalarExpr(E->getArg(0));
    // OGCG: prefetch(address, RW=0, Locality=3, Data=1)
    mlir::Value rw = builder.getConstant(loc, builder.getSInt32AttrLocalCached(0));
    mlir::Value locality =
        builder.getConstant(loc, builder.getSInt32AttrLocalCached(3));
    mlir::Value data =
        builder.getConstant(loc, builder.getSInt32AttrLocalCached(1));
    return emitIntrinsicCallOpCached(cgm, builder, loc, "prefetch",
                                     builder.getVoidTy(),
                                     mlir::ValueRange{address, rw, locality,
                                                      data});
  }

  if (BuiltinID == AArch64::BI__hlt) {
    mlir::Location loc = getLoc(E->getExprLoc());
    emitIntrinsicCallOpCached(cgm, builder, loc, "aarch64.hlt",
                              builder.getVoidTy(),
                              mlir::ValueRange{emitScalarExpr(E->getArg(0))});
    // Return 0, matching OGCG's behavior (MSVC returns undefined value).
    auto retTy = convertType(E->getType());
    if (retTy == builder.getSInt32Ty())
      return builder.getConstant(loc, builder.getSInt32AttrLocalCached(0));
    if (retTy == builder.getUInt32Ty())
      return builder.getConstant(loc, builder.getUInt32AttrLocalCached(0));
    if (retTy == builder.getSInt64Ty())
      return builder.getConstant(loc, builder.getSInt64AttrLocalCached(0));
    if (retTy == builder.getUInt64Ty())
      return builder.getConstant(loc, builder.getUInt64AttrLocalCached(0));
    return builder.getConstant(loc, cir::IntAttr::get(retTy, 0));
  }

  if (BuiltinID == AArch64::BI__builtin_arm_range_prefetch ||
      BuiltinID == AArch64::BI__builtin_arm_range_prefetch_x) {
    mlir::Location loc = getLoc(E->getExprLoc());
    llvm::SmallVector<mlir::Value, 4> ops;

    auto getIntArg = [&](unsigned argNo) {
      Expr::EvalResult result;
      if (!E->getArg(argNo)->EvaluateAsInt(result, cgm.getASTContext()))
        llvm_unreachable("Expected constant argument to range prefetch.");
      return result.Val.getInt().getExtValue();
    };

    ops.push_back(emitScalarExpr(E->getArg(0))); // Addr
    ops.push_back(emitScalarExpr(E->getArg(1))); // Access Kind
    ops.push_back(emitScalarExpr(E->getArg(2))); // Policy

    if (BuiltinID == AArch64::BI__builtin_arm_range_prefetch_x) {
      auto length = getIntArg(3);
      auto count = getIntArg(4) - 1;
      auto stride = getIntArg(5);
      auto distance = getIntArg(6);

      // Map ReuseDistance given in bytes to four bits representing decreasing
      // powers of two in the range 512MiB (0b0001) to 32KiB (0b1111). Values
      // are rounded up to the nearest power of 2, starting at 32KiB. Any value
      // over the maximum is represented by 0 (distance not known).
      if (distance > 0) {
        distance = llvm::Log2_32_Ceil(distance);
        if (distance < 15)
          distance = 15;
        else if (distance > 29)
          distance = 0;
        else
          distance = 30 - distance;
      }

      uint64_t mask22 = (1ULL << 22) - 1;
      uint64_t mask16 = (1ULL << 16) - 1;
      uint64_t metadata = (distance << 60) | ((stride & mask22) << 38) |
                          ((count & mask16) << 22) | (length & mask22);

      cir::IntType u64Ty = builder.getUIntNTy(64);
      ops.push_back(builder.getConstInt(loc, u64Ty, metadata));
    } else {
      ops.push_back(emitScalarExpr(E->getArg(3))); // Metadata
    }

    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("aarch64.range.prefetch"),
        mlir::Type{}, mlir::ValueRange{ops});
    return nullptr;
  }

  if (BuiltinID == NEON::BI__builtin_neon_vcvth_bf16_f32) {
    // Scalar bf16 from f32: bitcast arg to float, then fptrunc to bfloat.
    // Matches OGCG: Builder.CreateFPTrunc(Builder.CreateBitCast(..., FloatTy),
    //                                     BFloatTy)
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value src = emitScalarExpr(E->getArg(0));
    auto floatTy = cir::SingleType::get(builder.getContext());
    auto bf16Ty = cir::BF16Type::get(builder.getContext());
    src = builder.createBitcast(src, floatTy);
    return builder.createCast(loc, cir::CastKind::floating, src, bf16Ty);
  }

  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  if (std::optional<CIRGenFunction::MSVCIntrin> MsvcIntId =
          translateAarch64ToMsvcIntrin(BuiltinID))
    return emitMSVCBuiltinExpr(*MsvcIntId, E);

  // Some intrinsics are equivalent - if they are use the base intrinsic ID.
  auto It = llvm::find_if(NEONEquivalentIntrinsicMap, [BuiltinID](auto &P) {
    return P.first == BuiltinID;
  });
  if (It != end(NEONEquivalentIntrinsicMap))
    BuiltinID = It->second;

  // Find out if any arguments are required to be integer constant
  // expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  llvm::SmallVector<mlir::Value, 4> Ops;
  Address PtrOp0 = Address::invalid();
  for (unsigned i = 0, e = E->getNumArgs() - 1; i != e; i++) {
    if (i == 0) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld1_v:
      case NEON::BI__builtin_neon_vld1q_v:
      case NEON::BI__builtin_neon_vld1_dup_v:
      case NEON::BI__builtin_neon_vld1q_dup_v:
      case NEON::BI__builtin_neon_vld1_lane_v:
      case NEON::BI__builtin_neon_vld1q_lane_v:
      case NEON::BI__builtin_neon_vld2_v:
      case NEON::BI__builtin_neon_vld2q_v:
      case NEON::BI__builtin_neon_vld3_v:
      case NEON::BI__builtin_neon_vld3q_v:
      case NEON::BI__builtin_neon_vld4_v:
      case NEON::BI__builtin_neon_vld4q_v:
      case NEON::BI__builtin_neon_vld2_lane_v:
      case NEON::BI__builtin_neon_vld2q_lane_v:
      case NEON::BI__builtin_neon_vld3_lane_v:
      case NEON::BI__builtin_neon_vld3q_lane_v:
      case NEON::BI__builtin_neon_vld4_lane_v:
      case NEON::BI__builtin_neon_vld4q_lane_v:
      case NEON::BI__builtin_neon_vld2_dup_v:
      case NEON::BI__builtin_neon_vld2q_dup_v:
      case NEON::BI__builtin_neon_vld3_dup_v:
      case NEON::BI__builtin_neon_vld3q_dup_v:
      case NEON::BI__builtin_neon_vld4_dup_v:
      case NEON::BI__builtin_neon_vld4q_dup_v:
      case NEON::BI__builtin_neon_vld1_x2_v:
      case NEON::BI__builtin_neon_vld1q_x2_v:
      case NEON::BI__builtin_neon_vld1_x3_v:
      case NEON::BI__builtin_neon_vld1q_x3_v:
      case NEON::BI__builtin_neon_vld1_x4_v:
      case NEON::BI__builtin_neon_vld1q_x4_v:
      case NEON::BI__builtin_neon_vst1_v:
      case NEON::BI__builtin_neon_vst1q_v:
      case NEON::BI__builtin_neon_vst1_x2_v:
      case NEON::BI__builtin_neon_vst1q_x2_v:
      case NEON::BI__builtin_neon_vst1_x3_v:
      case NEON::BI__builtin_neon_vst1q_x3_v:
      case NEON::BI__builtin_neon_vst1_x4_v:
      case NEON::BI__builtin_neon_vst1q_x4_v:
      case NEON::BI__builtin_neon_vst1_lane_v:
      case NEON::BI__builtin_neon_vst1q_lane_v:
      case NEON::BI__builtin_neon_vldap1_lane_s64:
      case NEON::BI__builtin_neon_vldap1q_lane_s64:
      case NEON::BI__builtin_neon_vstl1_lane_s64:
      case NEON::BI__builtin_neon_vstl1q_lane_s64:
      case NEON::BI__builtin_neon_vst2_v:
      case NEON::BI__builtin_neon_vst2q_v:
      case NEON::BI__builtin_neon_vst2_lane_v:
      case NEON::BI__builtin_neon_vst2q_lane_v:
      case NEON::BI__builtin_neon_vst3_v:
      case NEON::BI__builtin_neon_vst3q_v:
      case NEON::BI__builtin_neon_vst3_lane_v:
      case NEON::BI__builtin_neon_vst3q_lane_v:
      case NEON::BI__builtin_neon_vst4_v:
      case NEON::BI__builtin_neon_vst4q_v:
      case NEON::BI__builtin_neon_vst4_lane_v:
      case NEON::BI__builtin_neon_vst4q_lane_v:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp0 = emitPointerWithAlignment(E->getArg(0));
        Ops.push_back(PtrOp0.emitRawPointer());
        continue;
      }
    }
    Ops.push_back(emitScalarOrConstFoldImmArg(ICEArguments, i, E->getArg(i)));
  }

  auto theSISDMap = ArrayRef(AArch64SISDIntrinsicMap);
  const ARMVectorIntrinsicInfo *builtinInfo = findARMVectorIntrinsicInMap(
      theSISDMap, BuiltinID, AArch64SISDIntrinsicsProvenSorted);

  if (builtinInfo) {
    Ops.push_back(emitScalarExpr(E->getArg(E->getNumArgs() - 1)));
    mlir::Value result =
        emitCommonNeonSISDBuiltinExpr(*this, *builtinInfo, Ops, E);
    assert(result && "SISD intrinsic should have been handled");
    return result;
  }

  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  NeonTypeFlags Type(0);
  if (std::optional<llvm::APSInt> Result =
          Arg->getIntegerConstantExpr(getContext()))
    // Determine the type of this overloaded NEON intrinsic.
    Type = NeonTypeFlags(Result->getZExtValue());

  bool usgn = Type.isUnsigned();

  // Handle non-overloaded intrinsics first.
  switch (BuiltinID) {
  default:
    break;
  case NEON::BI__builtin_neon_vcvt1_bf16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt1_high_bf16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt1_low_bf16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt1_f16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt1_high_f16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt1_low_f16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt2_bf16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt2_high_bf16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt2_low_bf16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt2_f16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt2_high_f16_mf8_fpm:
  case NEON::BI__builtin_neon_vcvt2_low_f16_mf8_fpm: {
    mlir::Location loc = getLoc(E->getExprLoc());
    auto resultTy = mlir::cast<cir::VectorType>(convertType(E->getType()));
    auto inputTy = mlir::cast<cir::VectorType>(Ops[0].getType());
    llvm::StringRef intrinsicName = "aarch64.neon.fp8.cvtl1";
    bool extractLow = false;

    switch (BuiltinID) {
    default:
      llvm_unreachable("unexpected NEON FP8 cvtl builtin");
    case NEON::BI__builtin_neon_vcvt1_bf16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt1_high_bf16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt1_f16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt1_high_f16_mf8_fpm:
      break;
    case NEON::BI__builtin_neon_vcvt1_low_bf16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt1_low_f16_mf8_fpm:
      extractLow = true;
      break;
    case NEON::BI__builtin_neon_vcvt2_bf16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt2_high_bf16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt2_f16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt2_high_f16_mf8_fpm:
      intrinsicName = "aarch64.neon.fp8.cvtl2";
      break;
    case NEON::BI__builtin_neon_vcvt2_low_bf16_mf8_fpm:
    case NEON::BI__builtin_neon_vcvt2_low_f16_mf8_fpm:
      intrinsicName = "aarch64.neon.fp8.cvtl2";
      extractLow = true;
      break;
    }

    if (extractLow) {
      inputTy =
          cir::VectorType::get(inputTy.getElementType(), inputTy.getSize() / 2);
      Ops[0] = emitAArch64VectorExtract(*this, loc, inputTy, Ops[0]);
    }

    return emitAArch64FP8NeonCall(*this, loc, {inputTy}, Ops, intrinsicName,
                                  resultTy, E);
  }
  case NEON::BI__builtin_neon_vcvt_mf8_f16_fpm:
  case NEON::BI__builtin_neon_vcvtq_mf8_f16_fpm: {
    mlir::Location loc = getLoc(E->getExprLoc());
    auto resultTy = mlir::cast<cir::VectorType>(convertType(E->getType()));
    auto argTy = mlir::cast<cir::VectorType>(Ops[0].getType());
    auto floatVecTy =
        cir::VectorType::get(getCIRGenModule().fP16Ty, argTy.getSize() / 2);
    return emitAArch64FP8NeonCall(*this, loc, {floatVecTy, floatVecTy}, Ops,
                                  "aarch64.neon.fp8.fcvtn", resultTy, E);
  }
  case NEON::BI__builtin_neon_vcvt_mf8_f32_fpm: {
    mlir::Location loc = getLoc(E->getExprLoc());
    auto resultTy = mlir::cast<cir::VectorType>(convertType(E->getType()));
    auto floatVecTy = mlir::cast<cir::VectorType>(Ops[0].getType());
    return emitAArch64FP8NeonCall(*this, loc, {floatVecTy, floatVecTy}, Ops,
                                  "aarch64.neon.fp8.fcvtn", resultTy, E);
  }
  case NEON::BI__builtin_neon_vcvt_high_mf8_f32_fpm: {
    mlir::Location loc = getLoc(E->getExprLoc());
    auto resultTy = mlir::cast<cir::VectorType>(convertType(E->getType()));
    auto floatVecTy = mlir::cast<cir::VectorType>(Ops[1].getType());
    Ops[0] = emitAArch64VectorInsert(*this, loc, resultTy, Ops[0]);
    return emitAArch64FP8NeonCall(
        *this, loc, {resultTy, floatVecTy, floatVecTy}, Ops,
        "aarch64.neon.fp8.fcvtn2", resultTy, E);
  }
  case NEON::BI__builtin_neon_vabsh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(0)));
    return cir::FAbsOp::create(builder, getLoc(E->getExprLoc()), Ops);
  }
  case NEON::BI__builtin_neon_vaddq_p128: {
    mlir::Location loc = getLoc(E->getExprLoc());
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return builder.createBinop(loc, Ops[0], cir::BinOpKind::Xor, Ops[1]);
  }
  case NEON::BI__builtin_neon_vldrq_p128: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value ptr = emitScalarExpr(E->getArg(0));
    cir::IntType int128Ty = builder.getUIntNTy(128);
    return builder.createAlignedLoad(loc, int128Ty, ptr,
                                     clang::CharUnits::fromQuantity(16));
  }
  case NEON::BI__builtin_neon_vstrq_p128: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value val = emitScalarExpr(E->getArg(1));
    mlir::Value ptr = builder.createPtrBitcast(Ops[0], val.getType());
    builder.createAlignedStore(loc, val, ptr,
                               clang::CharUnits::fromQuantity(16));
    return nullptr;
  }
  case NEON::BI__builtin_neon_vcvts_f32_u32:
  case NEON::BI__builtin_neon_vcvtd_f64_u64:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcvts_f32_s32:
  case NEON::BI__builtin_neon_vcvtd_f64_s64: {
    mlir::Value input = emitScalarExpr(E->getArg(0));
    bool is64 =
        mlir::cast<cir::IntType>(input.getType()).getWidth() == 64;
    mlir::Type fpTy = is64 ? static_cast<mlir::Type>(getCIRGenModule().doubleTy)
                           : static_cast<mlir::Type>(getCIRGenModule().floatTy);
    return builder.createCast(cir::CastKind::int_to_float, input, fpTy);
  }
  case NEON::BI__builtin_neon_vcvth_f16_u16:
  case NEON::BI__builtin_neon_vcvth_f16_u32:
  case NEON::BI__builtin_neon_vcvth_f16_u64:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcvth_f16_s16:
  case NEON::BI__builtin_neon_vcvth_f16_s32:
  case NEON::BI__builtin_neon_vcvth_f16_s64: {
    mlir::Value input = emitScalarExpr(E->getArg(0));
    mlir::Type fpTy = getCIRGenModule().fP16Ty;
    return builder.createCast(cir::CastKind::int_to_float, input, fpTy);
  }
  case NEON::BI__builtin_neon_vcvtah_u16_f16:
  case NEON::BI__builtin_neon_vcvtmh_u16_f16:
  case NEON::BI__builtin_neon_vcvtnh_u16_f16:
  case NEON::BI__builtin_neon_vcvtph_u16_f16:
  case NEON::BI__builtin_neon_vcvth_u16_f16:
  case NEON::BI__builtin_neon_vcvtah_s16_f16:
  case NEON::BI__builtin_neon_vcvtmh_s16_f16:
  case NEON::BI__builtin_neon_vcvtnh_s16_f16:
  case NEON::BI__builtin_neon_vcvtph_s16_f16:
  case NEON::BI__builtin_neon_vcvth_s16_f16: {
    mlir::Type intTy = builder.getSInt16Ty();
    mlir::Type fpTy = getCIRGenModule().fP16Ty;
    Ops.push_back(emitScalarExpr(E->getArg(0)));
    llvm::StringRef intrinName;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvtah_u16_f16:
      intrinName = "aarch64.neon.fcvtau"; break;
    case NEON::BI__builtin_neon_vcvtmh_u16_f16:
      intrinName = "aarch64.neon.fcvtmu"; break;
    case NEON::BI__builtin_neon_vcvtnh_u16_f16:
      intrinName = "aarch64.neon.fcvtnu"; break;
    case NEON::BI__builtin_neon_vcvtph_u16_f16:
      intrinName = "aarch64.neon.fcvtpu"; break;
    case NEON::BI__builtin_neon_vcvth_u16_f16:
      intrinName = "aarch64.neon.fcvtzu"; break;
    case NEON::BI__builtin_neon_vcvtah_s16_f16:
      intrinName = "aarch64.neon.fcvtas"; break;
    case NEON::BI__builtin_neon_vcvtmh_s16_f16:
      intrinName = "aarch64.neon.fcvtms"; break;
    case NEON::BI__builtin_neon_vcvtnh_s16_f16:
      intrinName = "aarch64.neon.fcvtns"; break;
    case NEON::BI__builtin_neon_vcvtph_s16_f16:
      intrinName = "aarch64.neon.fcvtps"; break;
    case NEON::BI__builtin_neon_vcvth_s16_f16:
      intrinName = "aarch64.neon.fcvtzs"; break;
    }
    return emitNeonCallCached(*this, {fpTy}, Ops, intrinName, intTy,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcaleh_f16:
  case NEON::BI__builtin_neon_vcalth_f16:
  case NEON::BI__builtin_neon_vcageh_f16:
  case NEON::BI__builtin_neon_vcagth_f16: {
    mlir::Type int32Ty = builder.getSInt32Ty();
    mlir::Type fpTy = getCIRGenModule().fP16Ty;
    mlir::Location loc = getLoc(E->getExprLoc());
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    llvm::StringRef intrinName;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcageh_f16:
      intrinName = "aarch64.neon.facge"; break;
    case NEON::BI__builtin_neon_vcagth_f16:
      intrinName = "aarch64.neon.facgt"; break;
    case NEON::BI__builtin_neon_vcaleh_f16:
      intrinName = "aarch64.neon.facge"; std::swap(Ops[0], Ops[1]); break;
    case NEON::BI__builtin_neon_vcalth_f16:
      intrinName = "aarch64.neon.facgt"; std::swap(Ops[0], Ops[1]); break;
    }
    Ops[0] = emitNeonCallCached(*this, {fpTy, fpTy}, Ops, intrinName, int32Ty,
                                loc);
    return builder.createIntCast(Ops[0], convertType(E->getType()));
  }
  case NEON::BI__builtin_neon_vcvth_n_s16_f16:
  case NEON::BI__builtin_neon_vcvth_n_u16_f16: {
    mlir::Type int32Ty = builder.getSInt32Ty();
    mlir::Type fpTy = getCIRGenModule().fP16Ty;
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    llvm::StringRef intrinName;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_s16_f16:
      intrinName = "aarch64.neon.vcvtfp2fxs"; break;
    case NEON::BI__builtin_neon_vcvth_n_u16_f16:
      intrinName = "aarch64.neon.vcvtfp2fxu"; break;
    }
    Ops[0] = emitNeonCallCached(*this, {fpTy, int32Ty}, Ops, intrinName, int32Ty,
                                getLoc(E->getExprLoc()));
    return builder.createIntCast(Ops[0], convertType(E->getType()));
  }
  case NEON::BI__builtin_neon_vcvth_n_f16_s16:
  case NEON::BI__builtin_neon_vcvth_n_f16_u16: {
    mlir::Type fpTy = getCIRGenModule().fP16Ty;
    mlir::Type int32Ty = builder.getSInt32Ty();
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    llvm::StringRef intrinName;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_f16_s16:
      intrinName = "aarch64.neon.vcvtfxs2fp";
      Ops[0] = builder.createIntCast(Ops[0], int32Ty);
      break;
    case NEON::BI__builtin_neon_vcvth_n_f16_u16:
      intrinName = "aarch64.neon.vcvtfxu2fp";
      Ops[0] = builder.createIntCast(Ops[0], builder.getUInt32Ty());
      break;
    }
    return emitNeonCallCached(*this, {int32Ty, int32Ty}, Ops, intrinName, fpTy,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vpaddd_s64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::VectorType vecTy = cir::VectorType::get(builder.getSIntNTy(64), 2);
    mlir::Value vec =
        builder.createBitcast(emitScalarExpr(E->getArg(0)), vecTy);
    mlir::Value idx0 =
        builder.getConstant(loc, builder.getSInt32AttrLocalCached(0));
    mlir::Value idx1 =
        builder.getConstant(loc, builder.getSInt32AttrLocalCached(1));
    mlir::Value lane0 = cir::VecExtractOp::create(builder, loc, vec, idx0);
    mlir::Value lane1 = cir::VecExtractOp::create(builder, loc, vec, idx1);
    return builder.createAdd(loc, lane0, lane1);
  }
  case NEON::BI__builtin_neon_vpaddd_f64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    auto dblTy = cir::DoubleType::get(builder.getContext());
    cir::VectorType vecTy = cir::VectorType::get(dblTy, 2);
    mlir::Value vec =
        builder.createBitcast(emitScalarExpr(E->getArg(0)), vecTy);
    mlir::Value idx0 = builder.getConstantInt(loc, builder.getSInt32Ty(), 0);
    mlir::Value idx1 = builder.getConstantInt(loc, builder.getSInt32Ty(), 1);
    mlir::Value lane0 = cir::VecExtractOp::create(builder, loc, vec, idx0);
    mlir::Value lane1 = cir::VecExtractOp::create(builder, loc, vec, idx1);
    return builder.createBinop(loc, lane0, cir::BinOpKind::Add, lane1);
  }
  case NEON::BI__builtin_neon_vpadds_f32: {
    mlir::Location loc = getLoc(E->getExprLoc());
    auto fltTy = cir::SingleType::get(builder.getContext());
    cir::VectorType vecTy = cir::VectorType::get(fltTy, 2);
    mlir::Value vec =
        builder.createBitcast(emitScalarExpr(E->getArg(0)), vecTy);
    mlir::Value idx0 = builder.getConstantInt(loc, builder.getSInt32Ty(), 0);
    mlir::Value idx1 = builder.getConstantInt(loc, builder.getSInt32Ty(), 1);
    mlir::Value lane0 = cir::VecExtractOp::create(builder, loc, vec, idx0);
    mlir::Value lane1 = cir::VecExtractOp::create(builder, loc, vec, idx1);
    return builder.createBinop(loc, lane0, cir::BinOpKind::Add, lane1);
  }
  case NEON::BI__builtin_neon_vceqzd_s64:
  case NEON::BI__builtin_neon_vceqzd_f64:
  case NEON::BI__builtin_neon_vceqzs_f32:
  case NEON::BI__builtin_neon_vceqzh_f16:
  case NEON::BI__builtin_neon_vcgezd_s64:
  case NEON::BI__builtin_neon_vcgezd_f64:
  case NEON::BI__builtin_neon_vcgezs_f32:
  case NEON::BI__builtin_neon_vcgezh_f16:
  case NEON::BI__builtin_neon_vclezd_s64:
  case NEON::BI__builtin_neon_vclezd_f64:
  case NEON::BI__builtin_neon_vclezs_f32:
  case NEON::BI__builtin_neon_vclezh_f16:
  case NEON::BI__builtin_neon_vcgtzd_s64:
  case NEON::BI__builtin_neon_vcgtzd_f64:
  case NEON::BI__builtin_neon_vcgtzs_f32:
  case NEON::BI__builtin_neon_vcgtzh_f16:
  case NEON::BI__builtin_neon_vcltzd_s64:
  case NEON::BI__builtin_neon_vcltzd_f64:
  case NEON::BI__builtin_neon_vcltzs_f32:
  case NEON::BI__builtin_neon_vcltzh_f16: {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::CmpOpKind kind;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vceqzd_s64:
    case NEON::BI__builtin_neon_vceqzd_f64:
    case NEON::BI__builtin_neon_vceqzs_f32:
    case NEON::BI__builtin_neon_vceqzh_f16:
      kind = cir::CmpOpKind::eq;
      break;
    case NEON::BI__builtin_neon_vcgezd_s64:
    case NEON::BI__builtin_neon_vcgezd_f64:
    case NEON::BI__builtin_neon_vcgezs_f32:
    case NEON::BI__builtin_neon_vcgezh_f16:
      kind = cir::CmpOpKind::ge;
      break;
    case NEON::BI__builtin_neon_vclezd_s64:
    case NEON::BI__builtin_neon_vclezd_f64:
    case NEON::BI__builtin_neon_vclezs_f32:
    case NEON::BI__builtin_neon_vclezh_f16:
      kind = cir::CmpOpKind::le;
      break;
    case NEON::BI__builtin_neon_vcgtzd_s64:
    case NEON::BI__builtin_neon_vcgtzd_f64:
    case NEON::BI__builtin_neon_vcgtzs_f32:
    case NEON::BI__builtin_neon_vcgtzh_f16:
      kind = cir::CmpOpKind::gt;
      break;
    case NEON::BI__builtin_neon_vcltzd_s64:
    case NEON::BI__builtin_neon_vcltzd_f64:
    case NEON::BI__builtin_neon_vcltzs_f32:
    case NEON::BI__builtin_neon_vcltzh_f16:
      kind = cir::CmpOpKind::lt;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    mlir::Value zero = builder.getNullValue(arg.getType(), loc);
    mlir::Value cmp = builder.createCompare(loc, kind, arg, zero);
    // NEON scalar compares return all-ones (-1) for true, 0 for false.
    // Determine result integer width based on the builtin suffix.
    unsigned resBits;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vceqzh_f16:
    case NEON::BI__builtin_neon_vcgezh_f16:
    case NEON::BI__builtin_neon_vclezh_f16:
    case NEON::BI__builtin_neon_vcgtzh_f16:
    case NEON::BI__builtin_neon_vcltzh_f16:
      resBits = 16;
      break;
    case NEON::BI__builtin_neon_vceqzs_f32:
    case NEON::BI__builtin_neon_vcgezs_f32:
    case NEON::BI__builtin_neon_vclezs_f32:
    case NEON::BI__builtin_neon_vcgtzs_f32:
    case NEON::BI__builtin_neon_vcltzs_f32:
      resBits = 32;
      break;
    default:
      resBits = 64;
      break;
    }
    mlir::Value allOnes;
    mlir::Value zeroRes;
    switch (resBits) {
    case 16:
      allOnes = builder.getConstant(loc, builder.getSInt16AttrLocalCached(-1));
      zeroRes = builder.getConstant(loc, builder.getSInt16AttrLocalCached(0));
      break;
    case 32:
      allOnes = builder.getConstant(loc, builder.getSInt32AttrLocalCached(-1));
      zeroRes = builder.getConstant(loc, builder.getSInt32AttrLocalCached(0));
      break;
    case 64:
      allOnes = builder.getConstant(loc, builder.getSInt64AttrLocalCached(-1));
      zeroRes = builder.getConstant(loc, builder.getSInt64AttrLocalCached(0));
      break;
    default:
      llvm_unreachable("unexpected scalar compare result width");
    }
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }

  case NEON::BI__builtin_neon_vceqzd_u64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = Ops[0];
    mlir::Value zero = builder.getNullValue(arg.getType(), loc);
    mlir::Value cmp = builder.createCompare(loc, cir::CmpOpKind::eq, arg, zero);
    mlir::Value allOnes = builder.getConstant(
        loc, builder.getUInt64AttrLocalCached(static_cast<uint64_t>(-1)));
    mlir::Value zeroRes =
        builder.getConstant(loc, builder.getUInt64AttrLocalCached(0));
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }
  case NEON::BI__builtin_neon_vceqd_f64:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcged_f64:
  case NEON::BI__builtin_neon_vcgtd_f64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::CmpOpKind kind;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vceqd_f64:
      kind = cir::CmpOpKind::eq;
      break;
    case NEON::BI__builtin_neon_vcled_f64:
      kind = cir::CmpOpKind::le;
      break;
    case NEON::BI__builtin_neon_vcltd_f64:
      kind = cir::CmpOpKind::lt;
      break;
    case NEON::BI__builtin_neon_vcged_f64:
      kind = cir::CmpOpKind::ge;
      break;
    case NEON::BI__builtin_neon_vcgtd_f64:
      kind = cir::CmpOpKind::gt;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    mlir::Value cmp = builder.createCompare(loc, kind, Ops[0], Ops[1]);
    mlir::Value allOnes =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(-1));
    mlir::Value zeroRes =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(0));
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }
  case NEON::BI__builtin_neon_vceqs_f32:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcges_f32:
  case NEON::BI__builtin_neon_vcgts_f32: {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::CmpOpKind kind;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vceqs_f32:
      kind = cir::CmpOpKind::eq;
      break;
    case NEON::BI__builtin_neon_vcles_f32:
      kind = cir::CmpOpKind::le;
      break;
    case NEON::BI__builtin_neon_vclts_f32:
      kind = cir::CmpOpKind::lt;
      break;
    case NEON::BI__builtin_neon_vcges_f32:
      kind = cir::CmpOpKind::ge;
      break;
    case NEON::BI__builtin_neon_vcgts_f32:
      kind = cir::CmpOpKind::gt;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    mlir::Value cmp = builder.createCompare(loc, kind, Ops[0], Ops[1]);
    mlir::Value allOnes =
        builder.getConstant(loc, builder.getSInt32AttrLocalCached(-1));
    mlir::Value zeroRes =
        builder.getConstant(loc, builder.getSInt32AttrLocalCached(0));
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }
  case NEON::BI__builtin_neon_vceqh_f16:
  case NEON::BI__builtin_neon_vcleh_f16:
  case NEON::BI__builtin_neon_vclth_f16:
  case NEON::BI__builtin_neon_vcgeh_f16:
  case NEON::BI__builtin_neon_vcgth_f16: {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::CmpOpKind kind;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vceqh_f16:
      kind = cir::CmpOpKind::eq;
      break;
    case NEON::BI__builtin_neon_vcleh_f16:
      kind = cir::CmpOpKind::le;
      break;
    case NEON::BI__builtin_neon_vclth_f16:
      kind = cir::CmpOpKind::lt;
      break;
    case NEON::BI__builtin_neon_vcgeh_f16:
      kind = cir::CmpOpKind::ge;
      break;
    case NEON::BI__builtin_neon_vcgth_f16:
      kind = cir::CmpOpKind::gt;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    mlir::Value cmp = builder.createCompare(loc, kind, Ops[0], Ops[1]);
    mlir::Value allOnes =
        builder.getConstant(loc, builder.getSInt16AttrLocalCached(-1));
    mlir::Value zeroRes =
        builder.getConstant(loc, builder.getSInt16AttrLocalCached(0));
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }
  case NEON::BI__builtin_neon_vceqd_s64:
  case NEON::BI__builtin_neon_vceqd_u64:
  case NEON::BI__builtin_neon_vcgtd_s64:
  case NEON::BI__builtin_neon_vcgtd_u64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vcged_u64:
  case NEON::BI__builtin_neon_vcged_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcled_s64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    cir::CmpOpKind kind;
    switch (BuiltinID) {
    case NEON::BI__builtin_neon_vceqd_s64:
    case NEON::BI__builtin_neon_vceqd_u64:
      kind = cir::CmpOpKind::eq;
      break;
    case NEON::BI__builtin_neon_vcgtd_s64:
    case NEON::BI__builtin_neon_vcgtd_u64:
      kind = cir::CmpOpKind::gt;
      break;
    case NEON::BI__builtin_neon_vcltd_s64:
    case NEON::BI__builtin_neon_vcltd_u64:
      kind = cir::CmpOpKind::lt;
      break;
    case NEON::BI__builtin_neon_vcged_u64:
    case NEON::BI__builtin_neon_vcged_s64:
      kind = cir::CmpOpKind::ge;
      break;
    case NEON::BI__builtin_neon_vcled_u64:
    case NEON::BI__builtin_neon_vcled_s64:
      kind = cir::CmpOpKind::le;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    mlir::Value cmp = builder.createCompare(loc, kind, Ops[0], Ops[1]);
    mlir::Value allOnes =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(-1));
    mlir::Value zeroRes =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(0));
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }
  case NEON::BI__builtin_neon_vtstd_s64:
  case NEON::BI__builtin_neon_vtstd_u64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    mlir::Value anded = builder.createAnd(loc, Ops[0], Ops[1]);
    mlir::Value zero = builder.getNullValue(anded.getType(), loc);
    mlir::Value cmp =
        builder.createCompare(loc, cir::CmpOpKind::ne, anded, zero);
    mlir::Value allOnes =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(-1));
    mlir::Value zeroRes =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(0));
    return builder.createSelect(loc, cmp, allOnes, zeroRes);
  }
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    Ops[0], Ops[2]);
  case NEON::BI__builtin_neon_vset_lane_mf8:
  case NEON::BI__builtin_neon_vsetq_lane_mf8: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value idx0 = builder.getConstantInt(loc, builder.getSInt32Ty(), 0);
    Ops[0] = cir::VecExtractOp::create(builder, loc, Ops[0], idx0);
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    return cir::VecInsertOp::create(builder, loc, Ops[1], Ops[0], Ops[2]);
  }
  case NEON::BI__builtin_neon_vset_lane_bf16:
  case NEON::BI__builtin_neon_vsetq_lane_bf16:
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    Ops[0], Ops[2]);

  case NEON::BI__builtin_neon_vset_lane_f64: {
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    Ops[1] = builder.createBitcast(Ops[1], cir::VectorType::get(doubleTy, 1));
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    Ops[0], Ops[2]);
  }
  case NEON::BI__builtin_neon_vsetq_lane_f64: {
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    Ops[1] = builder.createBitcast(Ops[1], cir::VectorType::get(doubleTy, 2));
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    Ops[0], Ops[2]);
  }
  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vdupb_lane_i8:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt8Ty, 8));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vdupb_laneq_i8:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt8Ty, 16));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_mf8:
  case NEON::BI__builtin_neon_vdupb_lane_mf8:
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_mf8:
  case NEON::BI__builtin_neon_vdupb_laneq_mf8:
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vduph_lane_i16:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt16Ty, 4));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vduph_laneq_i16:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt16Ty, 8));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vdups_lane_i32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt32Ty, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_f32:
  case NEON::BI__builtin_neon_vdups_lane_f32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(floatTy, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vdups_laneq_i32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt32Ty, 4));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vdupd_lane_i64:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt64Ty, 1));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vdupd_lane_f64:
  case NEON::BI__builtin_neon_vget_lane_f64:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(doubleTy, 1));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vdupd_laneq_i64:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(uInt64Ty, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vdups_laneq_f32:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(floatTy, 4));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vgetq_lane_f64:
  case NEON::BI__builtin_neon_vdupd_laneq_f64:
    Ops[0] = builder.createBitcast(Ops[0], cir::VectorType::get(doubleTy, 2));
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vaddh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return builder.createFAdd(getLoc(E->getExprLoc()), Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vsubh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return builder.createFSub(getLoc(E->getExprLoc()), Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vmulh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return builder.createFMul(getLoc(E->getExprLoc()), Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vdivh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return builder.createFDiv(getLoc(E->getExprLoc()), Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vfmah_f16: {
    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    // Ops[0] = a (accumulator), Ops[1] = b (pre-populated by common loop).
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    llvm::SmallVector<mlir::Value, 3> fmaOps = {Ops[1], Ops[2], Ops[0]};
    return emitNeonCallCached(*this, {}, fmaOps, "fma",
                              convertType(E->getType()),
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vfmsh_f16: {
    // vfms = fma(-b, c, a). Ops[0] = a, Ops[1] = b (pre-populated).
    mlir::Value negB = cir::UnaryOp::create(builder, getLoc(E->getExprLoc()),
                                             cir::UnaryOpKind::Minus, Ops[1]);
    mlir::Value c = emitScalarExpr(E->getArg(2));
    llvm::SmallVector<mlir::Value, 3> fmaOps = {negB, c, Ops[0]};
    return emitNeonCallCached(*this, {}, fmaOps, "fma",
                              convertType(E->getType()),
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vaddd_s64:
  case NEON::BI__builtin_neon_vaddd_u64:
    return builder.createAdd(getLoc(E->getExprLoc()), Ops[0],
                             emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vsubd_s64:
  case NEON::BI__builtin_neon_vsubd_u64:
    return builder.createSub(getLoc(E->getExprLoc()), Ops[0],
                             emitScalarExpr(E->getArg(1)));
  case NEON::BI__builtin_neon_vqdmlalh_s16:
  case NEON::BI__builtin_neon_vqdmlslh_s16: {
    mlir::Location loc = getLoc(E->getExprLoc());
    // vectorWrapScalar16: wrap i16 scalars into element 0 of v4i16 vectors
    cir::VectorType v4i16Ty = cir::VectorType::get(builder.getSInt16Ty(), 4);
    cir::VectorType v4i32Ty = cir::VectorType::get(builder.getSInt32Ty(), 4);
    mlir::Value arg2 = emitScalarExpr(E->getArg(2));
    mlir::Value wrappedB = Ops[1];
    vecExtendIntValue(*this, v4i16Ty, wrappedB, loc);
    vecExtendIntValue(*this, v4i16Ty, arg2, loc);
    llvm::SmallVector<mlir::Value, 2> productOps = {wrappedB, arg2};
    Ops[1] = emitNeonCallCached(*this, {v4i16Ty, v4i16Ty}, productOps,
                                "aarch64.neon.sqdmull", v4i32Ty, loc);
    mlir::Value zero = builder.getConstInt(loc, sizeTy, 0);
    Ops[1] = cir::VecExtractOp::create(builder, loc, Ops[1], zero);
    llvm::StringRef accumIntrinsic =
        BuiltinID == NEON::BI__builtin_neon_vqdmlalh_s16
            ? "aarch64.neon.sqadd"
            : "aarch64.neon.sqsub";
    mlir::Type int32Ty = builder.getSInt32Ty();
    return emitNeonCallCached(*this, {int32Ty, int32Ty}, Ops, accumIntrinsic,
                              int32Ty, loc);
  }
  case NEON::BI__builtin_neon_vqshlud_n_s64: {
    const cir::IntType IntType = builder.getSInt64Ty();
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    std::optional<llvm::APSInt> APSInt =
        E->getArg(1)->getIntegerConstantExpr(getContext());
    assert(APSInt && "Expected argument to be a constant");
    Ops[1] = builder.getSInt64(APSInt->getZExtValue(), getLoc(E->getExprLoc()));
    const StringRef Intrinsic = "aarch64.neon.sqshlu";
    return emitNeonCallCached(*this, {IntType, IntType}, Ops, Intrinsic,
                              IntType, getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vqshld_n_u64:
  case NEON::BI__builtin_neon_vqshld_n_s64: {
    cir::IntType IntType = BuiltinID == NEON::BI__builtin_neon_vqshld_n_u64
                               ? builder.getUInt64Ty()
                               : builder.getSInt64Ty();

    const StringRef Intrinsic = BuiltinID == NEON::BI__builtin_neon_vqshld_n_u64
                                    ? "aarch64.neon.uqshl"
                                    : "aarch64.neon.sqshl";
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    Ops[1] = builder.createIntCast(Ops[1], IntType);
    return emitNeonCallCached(*this, {IntType, IntType}, Ops, Intrinsic,
                              IntType, getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vrshrd_n_u64:
  case NEON::BI__builtin_neon_vrshrd_n_s64: {
    cir::IntType IntType = BuiltinID == NEON::BI__builtin_neon_vrshrd_n_u64
                               ? builder.getUInt64Ty()
                               : builder.getSInt64Ty();

    const StringRef Intrinsic = BuiltinID == NEON::BI__builtin_neon_vrshrd_n_u64
                                    ? "aarch64.neon.urshl"
                                    : "aarch64.neon.srshl";
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    std::optional<llvm::APSInt> APSInt =
        E->getArg(1)->getIntegerConstantExpr(getContext());
    assert(APSInt && "Expected argument to be a constant");
    int64_t SV = -APSInt->getSExtValue();
    Ops[1] = builder.getSInt64(SV, getLoc(E->getExprLoc()));
    return emitNeonCallCached(*this, {IntType, builder.getSInt64Ty()}, Ops,
                              Intrinsic, IntType, getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vrsrad_n_u64:
  case NEON::BI__builtin_neon_vrsrad_n_s64: {
    cir::IntType IntType = BuiltinID == NEON::BI__builtin_neon_vrsrad_n_u64
                               ? builder.getUInt64Ty()
                               : builder.getSInt64Ty();
    Ops[1] = builder.createBitcast(Ops[1], IntType);
    Ops.push_back(builder.createNeg(emitScalarExpr(E->getArg(2))));

    const StringRef Intrinsic = BuiltinID == NEON::BI__builtin_neon_vrsrad_n_u64
                                    ? "aarch64.neon.urshl"
                                    : "aarch64.neon.srshl";

    llvm::SmallVector<mlir::Value, 2> args = {
        Ops[1], builder.createIntCast(Ops[2], IntType)};
    Ops[1] = emitNeonCallCached(*this, {IntType, IntType}, args, Intrinsic,
                                IntType, getLoc(E->getExprLoc()));
    return builder.createAdd(getLoc(E->getExprLoc()), Ops[0],
                             builder.createBitcast(Ops[1], IntType));
  }
  case NEON::BI__builtin_neon_vshld_n_s64:
  case NEON::BI__builtin_neon_vshld_n_u64: {
    std::optional<llvm::APSInt> amt =
        E->getArg(1)->getIntegerConstantExpr(getContext());
    assert(amt && "Expected argument to be a constant");
    return builder.createShiftLeft(getLoc(E->getExprLoc()), Ops[0],
                                   amt->getZExtValue());
  }
  case NEON::BI__builtin_neon_vshrd_n_s64: {
    std::optional<llvm::APSInt> amt =
        E->getArg(1)->getIntegerConstantExpr(getContext());
    assert(amt && "Expected argument to be a constant");
    uint64_t bits = std::min(static_cast<uint64_t>(63), amt->getZExtValue());
    return builder.createShiftRight(getLoc(E->getExprLoc()), Ops[0], bits);
  }
  case NEON::BI__builtin_neon_vshrd_n_u64: {
    std::optional<llvm::APSInt> amt =
        E->getArg(1)->getIntegerConstantExpr(getContext());
    assert(amt && "Expected argument to be a constant");
    uint64_t shiftAmt = amt->getZExtValue();
    if (shiftAmt == 64)
      return builder.getConstInt(getLoc(E->getExprLoc()), builder.getUInt64Ty(),
                                 0);

    return builder.createShiftRight(getLoc(E->getExprLoc()), Ops[0], shiftAmt);
  }
  case NEON::BI__builtin_neon_vsrad_n_s64: {
    std::optional<llvm::APSInt> amt =
        E->getArg(2)->getIntegerConstantExpr(getContext());
    uint64_t shiftAmt =
        std::min(static_cast<uint64_t>(63), amt->getZExtValue());
    return builder.createAdd(
        getLoc(E->getExprLoc()), Ops[0],
        builder.createShift(getLoc(E->getExprLoc()), Ops[1], shiftAmt, false));
  }
  case NEON::BI__builtin_neon_vsrad_n_u64: {
    std::optional<llvm::APSInt> amt =
        E->getArg(2)->getIntegerConstantExpr(getContext());
    uint64_t shiftAmt = amt->getZExtValue();
    if (shiftAmt == 64)
      return Ops[0];

    return builder.createAdd(
        getLoc(E->getExprLoc()), Ops[0],
        builder.createShiftRight(getLoc(E->getExprLoc()), Ops[1], shiftAmt));
  }
  case NEON::BI__builtin_neon_vqdmlalh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlalh_laneq_s16:
  case NEON::BI__builtin_neon_vqdmlslh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlslh_laneq_s16: {
    mlir::Location loc = getLoc(E->getExprLoc());
    // Extract lane from vector arg
    Ops[2] = cir::VecExtractOp::create(builder, loc, Ops[2],
                                       emitScalarExpr(E->getArg(3)));
    // vectorWrapScalar16: wrap i16 scalars into element 0 of v4i16 vectors
    cir::VectorType v4i16Ty = cir::VectorType::get(builder.getSInt16Ty(), 4);
    cir::VectorType v4i32Ty = cir::VectorType::get(builder.getSInt32Ty(), 4);
    mlir::Value wrappedB = Ops[1];
    mlir::Value wrappedLane = Ops[2];
    vecExtendIntValue(*this, v4i16Ty, wrappedB, loc);
    vecExtendIntValue(*this, v4i16Ty, wrappedLane, loc);
    llvm::SmallVector<mlir::Value, 2> productOps = {wrappedB, wrappedLane};
    Ops[1] = emitNeonCallCached(*this, {v4i16Ty, v4i16Ty}, productOps,
                                "aarch64.neon.sqdmull", v4i32Ty, loc);
    mlir::Value zero = builder.getConstInt(loc, sizeTy, 0);
    Ops[1] = cir::VecExtractOp::create(builder, loc, Ops[1], zero);
    // Match OGCG: drop the extracted lane and keep {accumulator, product}.
    Ops.pop_back();
    llvm::StringRef accumIntrinsic =
        (BuiltinID == NEON::BI__builtin_neon_vqdmlalh_lane_s16 ||
         BuiltinID == NEON::BI__builtin_neon_vqdmlalh_laneq_s16)
            ? "aarch64.neon.sqadd"
            : "aarch64.neon.sqsub";
    mlir::Type int32Ty = builder.getSInt32Ty();
    return emitNeonCallCached(*this, {int32Ty, int32Ty}, Ops, accumIntrinsic,
                              int32Ty, loc);
  }
  case NEON::BI__builtin_neon_vqdmlals_s32:
  case NEON::BI__builtin_neon_vqdmlsls_s32: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Type int32Ty = builder.getSInt32Ty();
    mlir::Type int64Ty = builder.getSInt64Ty();
    llvm::SmallVector<mlir::Value, 2> productOps;
    productOps.push_back(Ops[1]);
    productOps.push_back(emitScalarExpr(E->getArg(2)));
    Ops[1] = emitNeonCallCached(*this, {int32Ty, int32Ty}, productOps,
                                "aarch64.neon.sqdmulls.scalar", int64Ty, loc);
    llvm::StringRef accumIntrinsic =
        BuiltinID == NEON::BI__builtin_neon_vqdmlals_s32
            ? "aarch64.neon.sqadd"
            : "aarch64.neon.sqsub";
    return emitNeonCallCached(*this, {int64Ty, int64Ty}, Ops, accumIntrinsic,
                              int64Ty, loc);
  }
  case NEON::BI__builtin_neon_vqdmlals_lane_s32:
  case NEON::BI__builtin_neon_vqdmlals_laneq_s32:
  case NEON::BI__builtin_neon_vqdmlsls_lane_s32:
  case NEON::BI__builtin_neon_vqdmlsls_laneq_s32: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Type int32Ty = builder.getSInt32Ty();
    mlir::Type int64Ty = builder.getSInt64Ty();
    // Extract lane from vector arg
    Ops[2] = cir::VecExtractOp::create(builder, loc, Ops[2],
                                       emitScalarExpr(E->getArg(3)));
    llvm::SmallVector<mlir::Value, 2> productOps;
    productOps.push_back(Ops[1]);
    productOps.push_back(Ops[2]);
    Ops[1] = emitNeonCallCached(*this, {int32Ty, int32Ty}, productOps,
                                "aarch64.neon.sqdmulls.scalar", int64Ty, loc);
    // Match OGCG: drop the extracted lane and keep {accumulator, product}.
    Ops.pop_back();
    llvm::StringRef accumIntrinsic =
        (BuiltinID == NEON::BI__builtin_neon_vqdmlals_lane_s32 ||
         BuiltinID == NEON::BI__builtin_neon_vqdmlals_laneq_s32)
            ? "aarch64.neon.sqadd"
            : "aarch64.neon.sqsub";
    return emitNeonCallCached(*this, {int64Ty, int64Ty}, Ops, accumIntrinsic,
                              int64Ty, loc);
  }
  case NEON::BI__builtin_neon_vget_lane_bf16:
  case NEON::BI__builtin_neon_vduph_lane_bf16:
  case NEON::BI__builtin_neon_vduph_lane_f16: {
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  }
  case NEON::BI__builtin_neon_vgetq_lane_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_f16: {
    return cir::VecExtractOp::create(builder, getLoc(E->getExprLoc()), Ops[0],
                                     emitScalarExpr(E->getArg(1)));
  }
  case NEON::BI__builtin_neon_vcvt_bf16_f32: {
    // <4 x float> -> <4 x bfloat>: bitcast input to v4f32, then fptrunc.
    mlir::Location loc = getLoc(E->getExprLoc());
    auto bf16Ty = cir::BF16Type::get(builder.getContext());
    auto floatTy = cir::SingleType::get(builder.getContext());
    auto v4f32 = cir::VectorType::get(floatTy, 4);
    auto v4bf16 = cir::VectorType::get(bf16Ty, 4);
    mlir::Value src = builder.createBitcast(Ops[0], v4f32);
    return builder.createCast(loc, cir::CastKind::floating, src, v4bf16);
  }
  case NEON::BI__builtin_neon_vcvtq_low_bf16_f32: {
    // <4 x float> -> <8 x bfloat> with zeros in upper half.
    // fptrunc to <4 x bfloat>, then concat with zero <4 x bfloat>.
    mlir::Location loc = getLoc(E->getExprLoc());
    auto bf16Ty = cir::BF16Type::get(builder.getContext());
    auto floatTy = cir::SingleType::get(builder.getContext());
    auto v4f32 = cir::VectorType::get(floatTy, 4);
    auto v4bf16 = cir::VectorType::get(bf16Ty, 4);
    mlir::Value src = builder.createBitcast(Ops[0], v4f32);
    mlir::Value trunc =
        builder.createCast(loc, cir::CastKind::floating, src, v4bf16);
    mlir::Value zero = builder.getZero(loc, v4bf16);
    llvm::SmallVector<int64_t, 8> mask = {0, 1, 2, 3, 4, 5, 6, 7};
    return builder.createVecShuffle(loc, trunc, zero, mask);
  }
  case NEON::BI__builtin_neon_vcvtq_high_bf16_f32: {
    // Preserve lower 4 bf16 from Ops[0], replace upper 4 with truncated f32s.
    // Ops[0] = existing <8 x bfloat>, Ops[1] = <4 x float>
    mlir::Location loc = getLoc(E->getExprLoc());
    auto bf16Ty = cir::BF16Type::get(builder.getContext());
    auto floatTy = cir::SingleType::get(builder.getContext());
    auto v4f32 = cir::VectorType::get(floatTy, 4);
    auto v4bf16 = cir::VectorType::get(bf16Ty, 4);
    auto v8bf16 = cir::VectorType::get(bf16Ty, 8);
    // Extract lower 4 lanes from existing vector.
    mlir::Value existing = builder.createBitcast(Ops[0], v8bf16);
    llvm::SmallVector<int64_t, 4> loMask = {0, 1, 2, 3};
    mlir::Value inactive =
        builder.createVecShuffle(loc, existing, existing, loMask);
    // Truncate new f32 values to bf16.
    mlir::Value src = builder.createBitcast(Ops[1], v4f32);
    mlir::Value trunc =
        builder.createCast(loc, cir::CastKind::floating, src, v4bf16);
    // Concatenate: lower 4 from inactive, upper 4 from trunc.
    llvm::SmallVector<int64_t, 8> concatMask = {0, 1, 2, 3, 4, 5, 6, 7};
    return builder.createVecShuffle(loc, inactive, trunc, concatMask);
  }

  case clang::AArch64::BI_InterlockedAdd:
  case clang::AArch64::BI_InterlockedAdd_acq:
  case clang::AArch64::BI_InterlockedAdd_rel:
  case clang::AArch64::BI_InterlockedAdd_nf:
  case clang::AArch64::BI_InterlockedAdd64:
  case clang::AArch64::BI_InterlockedAdd64_acq:
  case clang::AArch64::BI_InterlockedAdd64_rel:
  case clang::AArch64::BI_InterlockedAdd64_nf: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value ptr = Ops[0];
    mlir::Value val = emitScalarExpr(E->getArg(1));

    cir::MemOrder ordering;
    switch (BuiltinID) {
    case clang::AArch64::BI_InterlockedAdd:
    case clang::AArch64::BI_InterlockedAdd64:
      ordering = cir::MemOrder::SequentiallyConsistent;
      break;
    case clang::AArch64::BI_InterlockedAdd_acq:
    case clang::AArch64::BI_InterlockedAdd64_acq:
      ordering = cir::MemOrder::Acquire;
      break;
    case clang::AArch64::BI_InterlockedAdd_rel:
    case clang::AArch64::BI_InterlockedAdd64_rel:
      ordering = cir::MemOrder::Release;
      break;
    case clang::AArch64::BI_InterlockedAdd_nf:
    case clang::AArch64::BI_InterlockedAdd64_nf:
      ordering = cir::MemOrder::Relaxed;
      break;
    default:
      llvm_unreachable("missing builtin ID in switch!");
    }

    // Atomic fetch-add returns the old value.
    auto oldVal = cir::AtomicFetchOp::create(
        builder, loc, ptr, val, cir::AtomicFetchKind::Add,
        ordering, /*alignment=*/mlir::IntegerAttr{},
        /*is_volatile=*/false, /*fetch_first=*/true,
        /*sync_scope=*/nullptr);
    // _InterlockedAdd returns old + val = new value.
    return builder.createAdd(loc, oldVal->getResult(0), val);
  }
  }

  cir::VectorType ty = GetNeonType(this, Type);
  if (!ty)
    return std::nullopt;

  // Not all intrinsics handled by the common case work for AArch64 yet, so only
  // defer to common code if it's been added to our special map.
  builtinInfo = findARMVectorIntrinsicInMap(AArch64SIMDIntrinsicMap, BuiltinID,
                                            AArch64SIMDIntrinsicsProvenSorted);
  if (builtinInfo) {
    mlir::Value result = emitCommonNeonBuiltinExpr(
        builtinInfo->BuiltinID, builtinInfo->LLVMIntrinsic,
        builtinInfo->AltLLVMIntrinsic, builtinInfo->NameHint,
        builtinInfo->TypeModifier, E, Ops,
        /*never use addresses*/ Address::invalid(), Address::invalid(), Arch);
    // If emitCommonNeonBuiltinExpr handled it, return the result.
    // If it returned nullptr (intrinsic needs the AArch64-specific switch),
    // fall through to the switch below.
    if (result)
      return result;
  }

  if (mlir::Value V = emitAArch64TblBuiltinExpr(*this, BuiltinID, E, Ops, Arch))
    return V;

  cir::VectorType vTy = ty;
  llvm::SmallVector<mlir::Value, 4> args;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case NEON::BI__builtin_neon_vbsl_v:
  case NEON::BI__builtin_neon_vbslq_v: {
    cir::VectorType bitTy = vTy;
    if (cir::isAnyFloatingPointType(bitTy.getElementType()))
      bitTy = castVecOfFPTypeToVecOfIntWithSameWidth(builder, vTy);
    Ops[0] = builder.createBitcast(Ops[0], bitTy);
    Ops[1] = builder.createBitcast(Ops[1], bitTy);
    Ops[2] = builder.createBitcast(Ops[2], bitTy);

    mlir::Location loc = getLoc(E->getExprLoc());
    Ops[1] = builder.createAnd(loc, Ops[0], Ops[1]);
    Ops[2] = builder.createAnd(loc, builder.createNot(Ops[0]), Ops[2]);
    Ops[0] = builder.createOr(loc, Ops[1], Ops[2]);
    return builder.createBitcast(Ops[0], ty);
  }
  case NEON::BI__builtin_neon_vfma_lane_v:
  case NEON::BI__builtin_neon_vfmaq_lane_v: {
    // Swap: NEON has (addend, multiplicand, lane_source) but fma wants
    // (multiplicand, lane_splat, addend).
    mlir::Value addend = Ops[0];
    mlir::Value multiplicand = Ops[1];
    mlir::Value laneSource = Ops[2];

    // Determine source vector type for the lane operand.
    // For vfmaq_lane, the lane source is half width (e.g., float16x4 for float16x8).
    auto destVTy = mlir::cast<cir::VectorType>(ty);
    unsigned srcElts = (BuiltinID == NEON::BI__builtin_neon_vfmaq_lane_v)
                           ? destVTy.getSize() / 2
                           : destVTy.getSize();
    auto srcVTy = cir::VectorType::get(destVTy.getElementType(), srcElts);

    // Create splat from lane index
    auto laneConst = mlir::cast<cir::ConstantOp>(Ops[3].getDefiningOp());
    int64_t laneIdx = mlir::cast<cir::IntAttr>(laneConst.getValue())
                          .getUInt();
    llvm::SmallVector<int64_t> mask(destVTy.getSize(), laneIdx);
    laneSource = builder.createBitcast(laneSource, srcVTy);
    mlir::Value splatted = builder.createVecShuffle(
        getLoc(E->getExprLoc()), laneSource, mask);

    // Call fma(multiplicand, splatted, addend)
    Ops[0] = multiplicand;
    Ops[1] = splatted;
    Ops[2] = addend;
    Ops.pop_back(); // Remove lane index
    return emitNeonCallCached(*this, {ty, ty, ty}, Ops, "fma", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vfma_laneq_v: {
    // Similar to vfma_lane but lane source is wider (laneq = lane from quad).
    mlir::Value addend = Ops[0];
    mlir::Value multiplicand = Ops[1];
    mlir::Value laneSource = Ops[2];
    auto destVTy = mlir::cast<cir::VectorType>(ty);
    // Source is double width
    unsigned srcElts = destVTy.getSize() * 2;
    auto srcVTy = cir::VectorType::get(destVTy.getElementType(), srcElts);
    auto laneConst = mlir::cast<cir::ConstantOp>(Ops[3].getDefiningOp());
    int64_t laneIdx = mlir::cast<cir::IntAttr>(laneConst.getValue()).getUInt();
    llvm::SmallVector<int64_t> mask(destVTy.getSize(), laneIdx);
    laneSource = builder.createBitcast(laneSource, srcVTy);
    mlir::Value splatted = builder.createVecShuffle(
        getLoc(E->getExprLoc()), laneSource, mask);
    Ops[0] = multiplicand;
    Ops[1] = splatted;
    Ops[2] = addend;
    Ops.pop_back();
    return emitNeonCallCached(*this, {ty, ty, ty}, Ops, "fma", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vfmaq_laneq_v: {
    mlir::Location loc = getLoc(E->getExprLoc());

    // Bitcast all operands to actual vector type
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    Ops[2] = builder.createBitcast(Ops[2], vTy);

    // Splat the lane from Ops[2] using lane index Ops[3]
    Ops[2] = emitNeonSplat(builder, loc, Ops[2], Ops[3], vTy.getSize());

    // fma intrinsic order: (multiplicand1, multiplicand2, addend)
    // NEON order: (addend, multiplicand1, lane_vector) -> (Ops[0], Ops[1],
    // Ops[2]) After splat, Ops[2] is the splatted lane vector fma(Ops[2],
    // Ops[1], Ops[0]) = (v_lane * b) + a
    llvm::SmallVector<mlir::Value> fmaOps = {Ops[2], Ops[1], Ops[0]};
    llvm::SmallVector<mlir::Type> argTypes = {vTy, vTy, vTy};
    return emitNeonCallCached(*this, std::move(argTypes), fmaOps, "fma", vTy,
                              loc);
  }
  case NEON::BI__builtin_neon_vfmah_lane_f16:
  case NEON::BI__builtin_neon_vfmas_lane_f32:
  case NEON::BI__builtin_neon_vfmah_laneq_f16:
  case NEON::BI__builtin_neon_vfmas_laneq_f32:
  case NEON::BI__builtin_neon_vfmad_lane_f64:
  case NEON::BI__builtin_neon_vfmad_laneq_f64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    Ops.push_back(emitScalarExpr(E->getArg(3)));
    mlir::Type scalarTy = convertType(E->getCallReturnType(getContext()));
    // Extract the lane element from the vector
    Ops[2] = cir::VecExtractOp::create(builder, loc, Ops[2], Ops[3]);
    // fma(multiplicand1, multiplicand2, addend) = (b * lane) + a
    llvm::SmallVector<mlir::Value, 3> fmaOps = {Ops[1], Ops[2], Ops[0]};
    return emitNeonCallCached(*this, {scalarTy, scalarTy, scalarTy}, fmaOps,
                              "fma", scalarTy, loc);
  }
  case NEON::BI__builtin_neon_vmull_v: {
    llvm::StringRef name = usgn ? "aarch64.neon.umull" : "aarch64.neon.smull";
    if (Type.isPoly())
      name = "aarch64.neon.pmull";
    cir::VectorType argTy = builder.getExtendedOrTruncatedElementVectorType(
        ty, false /* truncated */, !usgn);
    return emitNeonCallCached(*this, {argTy, argTy}, Ops, name, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vmax_v:
  case NEON::BI__builtin_neon_vmaxq_v: {
    // OGCG uses aarch64.neon.fmax (IEEE 754-2008 maxNum, suppresses NaN),
    // NOT llvm.maximum (IEEE 754-2019, propagates NaN).
    llvm::StringRef name = usgn ? "aarch64.neon.umax" : "aarch64.neon.smax";
    if (cir::isFPOrVectorOfFPType(ty))
      name = "aarch64.neon.fmax";
    return emitNeonCallCached(*this, {ty, ty}, Ops, name, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vmaxh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return emitNeonCallCached(*this, {}, Ops, "aarch64.neon.fmax",
                              convertType(E->getType()),
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vmin_v:
  case NEON::BI__builtin_neon_vminq_v: {
    // OGCG uses aarch64.neon.fmin (IEEE 754-2008 minNum, suppresses NaN),
    // NOT llvm.minimum (IEEE 754-2019, propagates NaN).
    llvm::StringRef name = usgn ? "aarch64.neon.umin" : "aarch64.neon.smin";
    if (cir::isFPOrVectorOfFPType(ty))
      name = "aarch64.neon.fmin";
    return emitNeonCallCached(*this, {ty, ty}, Ops, name, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vminh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return emitNeonCallCached(*this, {}, Ops, "aarch64.neon.fmin",
                              convertType(E->getType()),
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vabd_v:
  case NEON::BI__builtin_neon_vabdq_v: {
    llvm::StringRef name = usgn ? "aarch64.neon.uabd" : "aarch64.neon.sabd";
    if (cir::isFPOrVectorOfFPType(ty))
      name = "aarch64.neon.fabd";
    return emitNeonCallCached(*this, {ty, ty}, Ops, name, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    cir::VectorType argTy = getHalfEltSizeTwiceNumElemsVecType(builder, vTy);
    mlir::Location loc = getLoc(E->getExprLoc());
    llvm::SmallVector<mlir::Value, 1> args = {Ops[1]};
    mlir::Value tmp = emitNeonCallCached(
        *this, {argTy}, args, usgn ? "aarch64.neon.uaddlp" : "aarch64.neon.saddlp",
        vTy, loc);
    mlir::Value addEnd = builder.createBitcast(Ops[0], vTy);
    return builder.createAdd(loc, tmp, addEnd);
  }
  case NEON::BI__builtin_neon_vpmin_v:
  case NEON::BI__builtin_neon_vpminq_v: {
    // Pairwise min: use uminp for unsigned, sminp for signed, fminp for float
    llvm::StringRef intName;
    if (mlir::isa<cir::IntType>(vTy.getElementType())) {
      intName = usgn ? "aarch64.neon.uminp" : "aarch64.neon.sminp";
    } else {
      intName = "aarch64.neon.fminp";
    }
    llvm::SmallVector<mlir::Value, 2> args = {Ops[0], Ops[1]};
    return emitNeonCallCached(*this, {vTy, vTy}, args, intName, vTy,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vpmax_v:
  case NEON::BI__builtin_neon_vpmaxq_v: {
    // Pairwise max: use umaxp for unsigned, smaxp for signed, fmaxp for float
    llvm::StringRef intName;
    if (mlir::isa<cir::IntType>(vTy.getElementType())) {
      intName = usgn ? "aarch64.neon.umaxp" : "aarch64.neon.smaxp";
    } else {
      intName = "aarch64.neon.fmaxp";
    }
    llvm::SmallVector<mlir::Value, 2> args = {Ops[0], Ops[1]};
    return emitNeonCallCached(*this, {vTy, vTy}, args, intName, vTy,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vminnm_v:
  case NEON::BI__builtin_neon_vminnmq_v:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops, "aarch64.neon.fminnm",
                              vTy, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vminnmh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    auto fp16Ty = cir::FP16Type::get(&getMLIRContext());
    return emitNeonCallCached(*this, {fp16Ty, fp16Ty}, Ops,
                              "aarch64.neon.fminnm", fp16Ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vmaxnm_v:
  case NEON::BI__builtin_neon_vmaxnmq_v:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops, "aarch64.neon.fmaxnm",
                              vTy, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vmaxnmh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    auto fp16Ty = cir::FP16Type::get(&getMLIRContext());
    return emitNeonCallCached(*this, {fp16Ty, fp16Ty}, Ops,
                              "aarch64.neon.fmaxnm", fp16Ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vamin_f16:
  case NEON::BI__builtin_neon_vaminq_f16:
  case NEON::BI__builtin_neon_vamin_f32:
  case NEON::BI__builtin_neon_vaminq_f32:
  case NEON::BI__builtin_neon_vaminq_f64:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops, "aarch64.neon.famin",
                              vTy, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vamax_f16:
  case NEON::BI__builtin_neon_vamaxq_f16:
  case NEON::BI__builtin_neon_vamax_f32:
  case NEON::BI__builtin_neon_vamaxq_f32:
  case NEON::BI__builtin_neon_vamaxq_f64:
    return emitNeonCallCached(*this, {vTy, vTy}, Ops, "aarch64.neon.famax",
                              vTy, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vrecpss_f32: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return emitNeonCallCached(*this, {floatTy, floatTy}, Ops,
                              "aarch64.neon.frecps", floatTy,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vrecpsd_f64:
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    return emitNeonCallCached(*this, {doubleTy, doubleTy}, Ops,
                              "aarch64.neon.frecps", doubleTy,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vrecpsh_f16: {
    Ops.push_back(emitScalarExpr(E->getArg(1)));
    auto fp16Ty = cir::FP16Type::get(&getMLIRContext());
    return emitNeonCallCached(*this, {fp16Ty, fp16Ty}, Ops,
                              "aarch64.neon.frecps", fp16Ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vqshrun_n_v: {
    // vqshrun_n: Saturating shift right unsigned narrow
    // Intrinsic: @llvm.aarch64.neon.sqshrun(input_vec, shift_imm)
    mlir::Location loc = getLoc(E->getExprLoc());

    // This is a narrowing operation: wider input -> narrower output
    // e.g., int16x8_t -> uint8x8_t
    llvm::SmallVector<mlir::Type> argTypes(2);
    argTypes[0] =
        builder.getExtendedOrTruncatedElementVectorType(ty, true, true);
    argTypes[1] = sInt32Ty;

    return emitNeonCallCached(*this, std::move(argTypes), Ops,
                              "aarch64.neon.sqshrun", vTy, loc);
  }
  case NEON::BI__builtin_neon_vqrshrun_n_v:
    // The prototype of builtin_neon_vqrshrun_n can be found at
    // https://developer.arm.com/architectures/instruction-sets/intrinsics/
    return emitNeonCallCached(
        *this,
        {builder.getExtendedOrTruncatedElementVectorType(ty, true, true),
         sInt32Ty},
        Ops, "aarch64.neon.sqrshrun", ty, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqshrn_n_v:
    return emitNeonCallCached(
        *this,
        {builder.getExtendedOrTruncatedElementVectorType(
             vTy, true /* extend */,
             mlir::cast<cir::IntType>(vTy.getElementType()).isSigned()),
         sInt32Ty},
        Ops, usgn ? "aarch64.neon.uqshrn" : "aarch64.neon.sqshrn", ty,
        getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vrshrn_n_v:
    return emitNeonCallCached(
        *this,
        {builder.getExtendedOrTruncatedElementVectorType(
             vTy, true /* extend */,
             mlir::cast<cir::IntType>(vTy.getElementType()).isSigned()),
         sInt32Ty},
        Ops, "aarch64.neon.rshrn", ty, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqrshrn_n_v:
    return emitNeonCallCached(
        *this,
        {builder.getExtendedOrTruncatedElementVectorType(
             vTy, true /* extend */,
             mlir::cast<cir::IntType>(vTy.getElementType()).isSigned()),
         sInt32Ty},
        Ops, usgn ? "aarch64.neon.uqrshrn" : "aarch64.neon.sqrshrn", ty,
        getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vrndah_f16:
    return cir::RoundOp::create(builder, getLoc(E->getExprLoc()),
                                emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrnda_v:
  case NEON::BI__builtin_neon_vrndaq_v: {
    // vrnda: round to nearest with ties away from zero
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return cir::RoundOp::create(builder, getLoc(E->getExprLoc()), Ops[0]);
  }
  case NEON::BI__builtin_neon_vrndih_f16:
    return cir::NearbyintOp::create(builder, getLoc(E->getExprLoc()),
                                    emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrndmh_f16:
    return cir::FloorOp::create(builder, getLoc(E->getExprLoc()),
                                emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrndm_v:
  case NEON::BI__builtin_neon_vrndmq_v: {
    // vrndm: round toward negative infinity (floor)
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return cir::FloorOp::create(builder, getLoc(E->getExprLoc()), Ops[0]);
  }
  case NEON::BI__builtin_neon_vrndnh_f16:
    return cir::RoundEvenOp::create(builder, getLoc(E->getExprLoc()),
                                    emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrndn_v:
  case NEON::BI__builtin_neon_vrndnq_v: {
    // vrndn: round to nearest with ties to even
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return cir::RoundEvenOp::create(builder, getLoc(E->getExprLoc()), Ops[0]);
  }
  case NEON::BI__builtin_neon_vrndns_f32: {
    mlir::Value arg0 = emitScalarExpr(E->getArg(0));
    // vrndns_f32: round to nearest with ties to even (scalar)
    return cir::RoundEvenOp::create(builder, getLoc(E->getExprLoc()), arg0);
  }
  case NEON::BI__builtin_neon_vrndph_f16:
    return cir::CeilOp::create(builder, getLoc(E->getExprLoc()),
                               emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrndp_v:
  case NEON::BI__builtin_neon_vrndpq_v: {
    // vrndp: round toward positive infinity (ceil)
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return cir::CeilOp::create(builder, getLoc(E->getExprLoc()), Ops[0]);
  }
  case NEON::BI__builtin_neon_vrndxh_f16:
    return cir::RintOp::create(builder, getLoc(E->getExprLoc()),
                               emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrndx_v:
  case NEON::BI__builtin_neon_vrndxq_v: {
    // vrndx: round to integral using current rounding mode (rint)
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return cir::RintOp::create(builder, getLoc(E->getExprLoc()), Ops[0]);
  }
  case NEON::BI__builtin_neon_vrndh_f16:
    return cir::TruncOp::create(builder, getLoc(E->getExprLoc()),
                                emitScalarExpr(E->getArg(0)));
  case NEON::BI__builtin_neon_vrnd_v:
  case NEON::BI__builtin_neon_vrndq_v: {
    // vrnd: round towards zero (truncate)
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return cir::TruncOp::create(builder, getLoc(E->getExprLoc()), Ops[0]);
  }
  case NEON::BI__builtin_neon_vcvt_f64_v:
  case NEON::BI__builtin_neon_vcvtq_f64_v: {
    Ops[0] = builder.createBitcast(Ops[0], ty);
    ty = GetNeonType(
        this, NeonTypeFlags(NeonTypeFlags::Float64, false, Type.isQuad()));
    return builder.createCast(cir::CastKind::int_to_float, Ops[0], ty);
  }
  case NEON::BI__builtin_neon_vcvt_f64_f32: {
    assert(Type.getEltType() == NeonTypeFlags::Float64 && Type.isQuad() &&
           "unexpected vcvt_f64_f32 builtin");
    NeonTypeFlags srcFlag = NeonTypeFlags(NeonTypeFlags::Float32, false, false);
    Ops[0] = builder.createBitcast(Ops[0], GetNeonType(this, srcFlag));
    return builder.createCast(cir::CastKind::floating, Ops[0], ty);
  }
  case NEON::BI__builtin_neon_vcvt_f32_f64: {
    assert(Type.getEltType() == NeonTypeFlags::Float32 &&
           "unexpected vcvt_f32_f64 builtin");
    NeonTypeFlags srcFlag = NeonTypeFlags(NeonTypeFlags::Float64, false, true);
    Ops[0] = builder.createBitcast(Ops[0], GetNeonType(this, srcFlag));
    return builder.createCast(cir::CastKind::floating, Ops[0], ty);
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    // Float to int conversion with round to zero
    llvm::StringRef intrincsName =
        usgn ? "aarch64.neon.fcvtzu" : "aarch64.neon.fcvtzs";

    // Determine the float source type based on result element type
    mlir::Type floatEltTy;
    switch (Type.getEltType()) {
    case NeonTypeFlags::Int16:
      floatEltTy = getCIRGenModule().fP16Ty;
      break;
    case NeonTypeFlags::Int32:
      floatEltTy = getCIRGenModule().floatTy;
      break;
    case NeonTypeFlags::Int64:
      floatEltTy = getCIRGenModule().doubleTy;
      break;
    default:
      llvm_unreachable("unexpected element type for fcvtz");
    }

    // Create the float vector type with same size as result type
    cir::VectorType floatVecTy = cir::VectorType::get(floatEltTy, ty.getSize());

    // Bitcast operand to float vector type
    Ops[0] = builder.createBitcast(Ops[0], floatVecTy);

    return emitNeonCallCached(*this, {floatVecTy}, Ops, intrincsName, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvta_s16_f16:
  case NEON::BI__builtin_neon_vcvta_u16_f16:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_f16:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_f16:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v: {
    // Float to int conversion with round to nearest, ties away
    llvm::StringRef intrincsName =
        usgn ? "aarch64.neon.fcvtau" : "aarch64.neon.fcvtas";

    mlir::Type floatEltTy;
    switch (Type.getEltType()) {
    case NeonTypeFlags::Int16:
      floatEltTy = getCIRGenModule().fP16Ty;
      break;
    case NeonTypeFlags::Int32:
      floatEltTy = getCIRGenModule().floatTy;
      break;
    case NeonTypeFlags::Int64:
      floatEltTy = getCIRGenModule().doubleTy;
      break;
    default:
      llvm_unreachable("unexpected element type for fcvta");
    }

    cir::VectorType floatVecTy = cir::VectorType::get(floatEltTy, ty.getSize());
    Ops[0] = builder.createBitcast(Ops[0], floatVecTy);

    return emitNeonCallCached(*this, {floatVecTy}, Ops, intrincsName, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvtm_s16_f16:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_f16:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtm_u16_f16:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_f16:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    // Float to int conversion with round toward -infinity
    llvm::StringRef intrincsName =
        usgn ? "aarch64.neon.fcvtmu" : "aarch64.neon.fcvtms";

    mlir::Type floatEltTy;
    switch (Type.getEltType()) {
    case NeonTypeFlags::Int16:
      floatEltTy = getCIRGenModule().fP16Ty;
      break;
    case NeonTypeFlags::Int32:
      floatEltTy = getCIRGenModule().floatTy;
      break;
    case NeonTypeFlags::Int64:
      floatEltTy = getCIRGenModule().doubleTy;
      break;
    default:
      llvm_unreachable("unexpected element type for fcvtm");
    }

    cir::VectorType floatVecTy = cir::VectorType::get(floatEltTy, ty.getSize());
    Ops[0] = builder.createBitcast(Ops[0], floatVecTy);

    return emitNeonCallCached(*this, {floatVecTy}, Ops, intrincsName, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvtn_s16_f16:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_f16:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtn_u16_f16:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_f16:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v: {
    // Float to int conversion with round to nearest
    llvm::StringRef intrincsName =
        usgn ? "aarch64.neon.fcvtnu" : "aarch64.neon.fcvtns";

    // Determine the float source type based on result element type
    mlir::Type floatEltTy;
    switch (Type.getEltType()) {
    case NeonTypeFlags::Int16:
      floatEltTy = getCIRGenModule().fP16Ty;
      break;
    case NeonTypeFlags::Int32:
      floatEltTy = getCIRGenModule().floatTy;
      break;
    case NeonTypeFlags::Int64:
      floatEltTy = getCIRGenModule().doubleTy;
      break;
    default:
      llvm_unreachable("unexpected element type for fcvtn");
    }

    // Create the float vector type with same size as result type
    cir::VectorType floatVecTy = cir::VectorType::get(floatEltTy, ty.getSize());

    // Bitcast operand to float vector type
    Ops[0] = builder.createBitcast(Ops[0], floatVecTy);

    return emitNeonCallCached(*this, {floatVecTy}, Ops, intrincsName, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vcvtp_s16_f16:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_f16:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtp_u16_f16:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_f16:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v: {
    // Float to int conversion with round toward +infinity
    llvm::StringRef intrincsName =
        usgn ? "aarch64.neon.fcvtpu" : "aarch64.neon.fcvtps";

    mlir::Type floatEltTy;
    switch (Type.getEltType()) {
    case NeonTypeFlags::Int16:
      floatEltTy = getCIRGenModule().fP16Ty;
      break;
    case NeonTypeFlags::Int32:
      floatEltTy = getCIRGenModule().floatTy;
      break;
    case NeonTypeFlags::Int64:
      floatEltTy = getCIRGenModule().doubleTy;
      break;
    default:
      llvm_unreachable("unexpected element type for fcvtp");
    }

    cir::VectorType floatVecTy = cir::VectorType::get(floatEltTy, ty.getSize());
    Ops[0] = builder.createBitcast(Ops[0], floatVecTy);

    return emitNeonCallCached(*this, {floatVecTy}, Ops, intrincsName, ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vmulx_v:
  case NEON::BI__builtin_neon_vmulxq_v: {
    return emitNeonCallCached(*this, {ty, ty}, Ops, "aarch64.neon.fmulx", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vmulxh_lane_f16:
  case NEON::BI__builtin_neon_vmulxh_laneq_f16: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Type fp16Ty = getCIRGenModule().fP16Ty;
    // Extract lane element from vector, emit lane index
    Ops.push_back(emitScalarExpr(E->getArg(2)));
    Ops[1] = cir::VecExtractOp::create(builder, loc, Ops[1], Ops[2]);
    Ops.pop_back();
    return emitNeonCallCached(*this, {fp16Ty, fp16Ty}, Ops,
                              "aarch64.neon.fmulx", fp16Ty, loc);
  }
  case NEON::BI__builtin_neon_vmul_lane_v:
  case NEON::BI__builtin_neon_vmul_laneq_v: {
    // v1f64 vmul_lane: bitcast to double, extract lane, fmul, bitcast back
    mlir::Location loc = getLoc(E->getExprLoc());
    Ops[0] = builder.createBitcast(Ops[0], doubleTy);
    bool isQuad = (BuiltinID == NEON::BI__builtin_neon_vmul_laneq_v);
    unsigned numLanes = isQuad ? 2 : 1;
    cir::VectorType dblVecTy = cir::VectorType::get(doubleTy, numLanes);
    Ops[1] = builder.createBitcast(Ops[1], dblVecTy);
    Ops[1] = cir::VecExtractOp::create(builder, loc, Ops[1], Ops[2]);
    mlir::Value result = builder.createFMul(loc, Ops[0], Ops[1]);
    return builder.createBitcast(result, ty);
  }
  case NEON::BI__builtin_neon_vnegd_s64: {
    mlir::Location loc = getLoc(E->getExprLoc());
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    mlir::Value zero = builder.getNullValue(arg.getType(), loc);
    return builder.createSub(loc, zero, arg);
  }
  case NEON::BI__builtin_neon_vnegh_f16: {
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return cir::UnaryOp::create(builder, getLoc(E->getExprLoc()),
                                arg.getType(), cir::UnaryOpKind::Minus, arg);
  }
  case NEON::BI__builtin_neon_vpmaxnm_v:
  case NEON::BI__builtin_neon_vpmaxnmq_v: {
    return emitNeonCallCached(*this, {ty, ty}, Ops, "aarch64.neon.fmaxnmp", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vpminnm_v:
  case NEON::BI__builtin_neon_vpminnmq_v: {
    return emitNeonCallCached(*this, {ty, ty}, Ops, "aarch64.neon.fminnmp", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vsqrth_f16: {
    mlir::Value arg = emitScalarExpr(E->getArg(0));
    return cir::SqrtOp::create(builder, getLoc(E->getExprLoc()),
                               arg.getType(), arg)
        .getResult();
  }
  case NEON::BI__builtin_neon_vsqrt_v:
  case NEON::BI__builtin_neon_vsqrtq_v: {
    Ops[0] = builder.createBitcast(Ops[0], ty);
    return emitNeonCallToOp<cir::SqrtOp>(builder, {ty}, Ops, std::nullopt, ty,
                                         getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vrbit_v:
  case NEON::BI__builtin_neon_vrbitq_v: {
    return emitNeonCallCached(*this, {ty}, Ops, "bitreverse", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vaddv_u8:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddv_s8: {
    cir::IntType eltTy = usgn ? uInt8Ty : sInt8Ty;
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.add", eltTy, 8, E);
  }
  case NEON::BI__builtin_neon_vaddv_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddv_s16: {
    cir::IntType eltTy = usgn ? uInt16Ty : sInt16Ty;
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.add", eltTy, 4, E);
  }
  case NEON::BI__builtin_neon_vaddvq_u8:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddvq_s8: {
    cir::IntType eltTy = usgn ? uInt8Ty : sInt8Ty;
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.add", eltTy, 16,
                                       E);
  }
  case NEON::BI__builtin_neon_vaddvq_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddvq_s16: {
    cir::IntType eltTy = usgn ? uInt16Ty : sInt16Ty;
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.add", eltTy, 8, E);
  }
  case NEON::BI__builtin_neon_vmaxv_u8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umax", uInt8Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxv_u16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umax", uInt16Ty, 4,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxvq_u8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umax", uInt8Ty, 16,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxvq_u16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umax", uInt16Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxv_s8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smax", sInt8Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxv_s16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smax", sInt16Ty, 4,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxvq_s8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smax", sInt8Ty, 16,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxvq_s16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smax", sInt16Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vmaxv_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fmaxv",
                                       getCIRGenModule().fP16Ty, 4, E);
  }
  case NEON::BI__builtin_neon_vmaxvq_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fmaxv",
                                       getCIRGenModule().fP16Ty, 8, E);
  }
  case NEON::BI__builtin_neon_vminv_u8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umin", uInt8Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vminv_u16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umin", uInt16Ty, 4,
                                       E);
  }
  case NEON::BI__builtin_neon_vminvq_u8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umin", uInt8Ty, 16,
                                       E);
  }
  case NEON::BI__builtin_neon_vminvq_u16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.umin", uInt16Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vminv_s8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smin", sInt8Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vminv_s16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smin", sInt16Ty, 4,
                                       E);
  }
  case NEON::BI__builtin_neon_vminvq_s8: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smin", sInt8Ty, 16,
                                       E);
  }
  case NEON::BI__builtin_neon_vminvq_s16: {
    return emitCommonNeonVecAcrossCall(*this, "vector.reduce.smin", sInt16Ty, 8,
                                       E);
  }
  case NEON::BI__builtin_neon_vminv_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fminv",
                                       getCIRGenModule().fP16Ty, 4, E);
  }
  case NEON::BI__builtin_neon_vminvq_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fminv",
                                       getCIRGenModule().fP16Ty, 8, E);
  }
  case NEON::BI__builtin_neon_vmaxnmv_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fmaxnmv",
                                       getCIRGenModule().fP16Ty, 4, E);
  }
  case NEON::BI__builtin_neon_vmaxnmvq_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fmaxnmv",
                                       getCIRGenModule().fP16Ty, 8, E);
  }
  case NEON::BI__builtin_neon_vminnmv_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fminnmv",
                                       getCIRGenModule().fP16Ty, 4, E);
  }
  case NEON::BI__builtin_neon_vminnmvq_f16: {
    return emitCommonNeonVecAcrossCall(*this, "aarch64.neon.fminnmv",
                                       getCIRGenModule().fP16Ty, 8, E);
  }
  case NEON::BI__builtin_neon_vmul_n_f64: {
    // vmul_n_f64: multiply v1f64 vector by scalar f64.
    // OGCG bitcasts v1f64 to f64, does scalar fmul, bitcasts back.
    Ops[0] = builder.createBitcast(Ops[0], getCIRGenModule().doubleTy);
    mlir::Value rhs = emitScalarExpr(E->getArg(1));
    mlir::Value result =
        builder.createFMul(getLoc(E->getExprLoc()), Ops[0], rhs);
    return builder.createBitcast(result, ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_u8: {
    cir::VectorType vTy = cir::VectorType::get(uInt8Ty, 16);
    Ops.push_back(emitScalarExpr(E->getArg(0)));
    Ops[0] = emitNeonCallCached(*this, {vTy}, Ops, "aarch64.neon.uaddlv",
                                uInt32Ty, getLoc(E->getExprLoc()));
    return builder.createIntCast(Ops[0], uInt16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddlvq_s16: {
    mlir::Type argTy = cir::VectorType::get(usgn ? uInt16Ty : sInt16Ty, 8);
    llvm::SmallVector<mlir::Value, 1> argOps = {emitScalarExpr(E->getArg(0))};
    return emitNeonCallCached(
        *this, {argTy}, argOps,
        usgn ? "aarch64.neon.uaddlv" : "aarch64.neon.saddlv",
        usgn ? uInt32Ty : sInt32Ty, getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vaddlv_u8:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddlv_s8: {
    cir::VectorType vTy = cir::VectorType::get(usgn ? uInt8Ty : sInt8Ty, 8);
    Ops.push_back(emitScalarExpr(E->getArg(0)));
    Ops[0] = emitNeonCallCached(
        *this, {vTy}, Ops, usgn ? "aarch64.neon.uaddlv" : "aarch64.neon.saddlv",
        usgn ? uInt32Ty : sInt32Ty, getLoc(E->getExprLoc()));
    return builder.createIntCast(Ops[0], usgn ? uInt16Ty : sInt16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddlv_s16: {
    mlir::Type argTy = cir::VectorType::get(usgn ? uInt16Ty : sInt16Ty, 4);
    llvm::SmallVector<mlir::Value, 1> argOps = {emitScalarExpr(E->getArg(0))};
    return emitNeonCallCached(
        *this, {argTy}, argOps,
        usgn ? "aarch64.neon.uaddlv" : "aarch64.neon.saddlv",
        usgn ? uInt32Ty : sInt32Ty, getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vaddlvq_s8: {
    cir::VectorType vTy = cir::VectorType::get(sInt8Ty, 16);
    Ops.push_back(emitScalarExpr(E->getArg(0)));
    Ops[0] = emitNeonCallCached(*this, {vTy}, Ops, "aarch64.neon.saddlv",
                                sInt32Ty, getLoc(E->getExprLoc()));
    return builder.createIntCast(Ops[0], sInt16Ty);
  }
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v: {
    mlir::Location loc = getLoc(E->getExprLoc());

    // Bitcast operands to actual vector type
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = builder.createBitcast(Ops[1], vTy);

    llvm::SmallVector<mlir::Type> argTypes = {vTy, vTy, Ops[2].getType()};

    return emitNeonCallCached(*this, std::move(argTypes), Ops,
                              "aarch64.neon.vsri", vTy, loc);
  }
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v: {
    mlir::Location loc = getLoc(E->getExprLoc());

    // Bitcast operands to actual vector type
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = builder.createBitcast(Ops[1], vTy);

    llvm::SmallVector<mlir::Type> argTypes = {vTy, vTy, Ops[2].getType()};

    return emitNeonCallCached(*this, std::move(argTypes), Ops,
                              "aarch64.neon.vsli", vTy, loc);
  }
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v: {
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    Ops[1] = emitNeonRShiftImm(*this, Ops[1], Ops[2], vTy, usgn,
                               getLoc(E->getExprLoc()));
    return builder.createAdd(getLoc(E->getExprLoc()), Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v: {
    llvm::SmallVector<mlir::Value> tmpOps = {Ops[1], Ops[2]};
    // The llvm intrinsic is expecting negative shift amount for right shift.
    // Thus we have to make shift amount vec type to be signed.
    cir::VectorType shitAmtVecTy =
        usgn ? getSignChangedVectorType(builder, vTy) : vTy;
    mlir::Value tmp = emitNeonCallCached(
        *this, {vTy, shitAmtVecTy}, tmpOps,
        usgn ? "aarch64.neon.urshl" : "aarch64.neon.srshl", vTy,
        getLoc(E->getExprLoc()), false, 1 /* shift amount is args[1]*/,
        true /* right shift */);
    Ops[0] = builder.createBitcast(Ops[0], vTy);
    return builder.createBinop(getLoc(E->getExprLoc()), Ops[0],
                               cir::BinOpKind::Add, tmp);
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    return builder.createAlignedLoad(Ops[0].getLoc(), vTy, Ops[0],
                                     PtrOp0.getAlignment());
  }
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v: {
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    // Bitcast pointer to match the vector type
    Address storeAddr = PtrOp0.withElementType(builder, vTy);
    (void)builder.createAlignedStore(
        Ops[1].getLoc(), Ops[1], storeAddr.getPointer(), PtrOp0.getAlignment());
    return Ops[1];
  }
  case NEON::BI__builtin_neon_vld1_lane_v:
  case NEON::BI__builtin_neon_vld1q_lane_v: {
    // Use the actual NEON element type (from vTy) for the load, not the
    // builtin's declared return type (which is always signed char vector).
    // Bitcast source vector to actual NEON type FIRST (must come before scalar
    // load to match expected LLVM IR order)
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    // Bitcast pointer to the actual element type
    mlir::Value ptr = Ops[0];
    if (vTy.getElementType() !=
        mlir::cast<cir::PointerType>(ptr.getType()).getPointee())
      ptr = builder.createPtrBitcast(ptr, vTy.getElementType());
    // Load using actual element type
    mlir::Value loadedVal = builder.createAlignedLoad(
        ptr.getLoc(), vTy.getElementType(), ptr, PtrOp0.getAlignment());
    // Insert into actual NEON type (will be bitcasted to return type in
    // emitBuiltinExpr)
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    loadedVal, Ops[2]);
  }
  case NEON::BI__builtin_neon_vldap1_lane_s64:
  case NEON::BI__builtin_neon_vldap1q_lane_s64: {
    // Load with acquire ordering (LDAPUR instruction)
    // Order: ptr bitcast → atomic load → vec bitcast → vec.insert
    // Bitcast pointer to the actual element type
    mlir::Value ptr = Ops[0];
    if (vTy.getElementType() !=
        mlir::cast<cir::PointerType>(ptr.getType()).getPointee())
      ptr = builder.createPtrBitcast(ptr, vTy.getElementType());
    // Create atomic load with acquire ordering
    mlir::IntegerAttr alignAttr =
        builder.getAlignmentAttr(PtrOp0.getAlignment());
    auto acquireOrder =
        cir::MemOrderAttr::get(builder.getContext(), cir::MemOrder::Acquire);
    auto systemScope = cir::SyncScopeKindAttr::get(builder.getContext(),
                                                   cir::SyncScopeKind::System);
    cir::LoadOp load = cir::LoadOp::create(
        builder, getLoc(E->getExprLoc()), ptr,
        /*isDeref=*/false, /*isVolatile=*/false,
        /*is_nontemporal=*/false, alignAttr, systemScope, acquireOrder,
        /*tbaa=*/{});
    // Bitcast source vector to actual NEON type (after atomic load)
    Ops[1] = builder.createBitcast(Ops[1], vTy);
    // Insert into actual NEON type (will be bitcasted to return type in
    // emitBuiltinExpr)
    return cir::VecInsertOp::create(builder, getLoc(E->getExprLoc()), Ops[1],
                                    load, Ops[2]);
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Address ptrAddr = PtrOp0.withElementType(builder, vTy.getElementType());
    mlir::Value val = builder.createLoad(getLoc(E->getExprLoc()), ptrAddr);
    cir::VecSplatOp vecSplat =
        cir::VecSplatOp::create(builder, getLoc(E->getExprLoc()), vTy, val);
    return vecSplat;
  }
  case NEON::BI__builtin_neon_vst1_lane_v:
  case NEON::BI__builtin_neon_vst1q_lane_v: {
    Ops[1] = builder.createBitcast(Ops[1], ty);
    Ops[1] =
        cir::VecExtractOp::create(builder, Ops[1].getLoc(), Ops[1], Ops[2]);
    // Bitcast pointer to match the element type
    Address storeAddr = PtrOp0.withElementType(builder, ty.getElementType());
    (void)builder.createAlignedStore(getLoc(E->getExprLoc()), Ops[1],
                                     storeAddr.getPointer(),
                                     PtrOp0.getAlignment());
    return Ops[1];
  }
  case NEON::BI__builtin_neon_vstl1_lane_s64:
  case NEON::BI__builtin_neon_vstl1q_lane_s64: {
    // Store with release ordering (STLUR instruction)
    Ops[1] = builder.createBitcast(Ops[1], ty);
    Ops[1] =
        cir::VecExtractOp::create(builder, Ops[1].getLoc(), Ops[1], Ops[2]);
    // Bitcast pointer to match the element type
    Address storeAddr = PtrOp0.withElementType(builder, ty.getElementType());
    // Create atomic store with release ordering
    mlir::IntegerAttr alignAttr =
        builder.getAlignmentAttr(PtrOp0.getAlignment());
    auto releaseOrder =
        cir::MemOrderAttr::get(builder.getContext(), cir::MemOrder::Release);
    (void)builder.CIRBaseBuilderTy::createStore(
        getLoc(E->getExprLoc()), Ops[1], storeAddr.getPointer(),
        /*isVolatile=*/false, alignAttr, /*scope=*/{}, releaseOrder);
    return Ops[1];
  }
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v: {
    // vld2: Load 2-element structure with deinterleaving
    // Loads interleaved data [a0,b0,a1,b1,...] into two vectors [a0,a1,...] and
    // [b0,b1,...]
    mlir::Location loc = getLoc(E->getExprLoc());

    // Create struct type: { vec, vec }
    llvm::SmallVector<mlir::Type, 2> members = {vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Call intrinsic: @llvm.aarch64.neon.ld2.<vectype>(ptr)
    llvm::SmallVector<mlir::Type> argTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> args = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld2", structTy, loc);

    // Store result to destination pointer with proper alignment
    // Bitcast pointer to match the struct type
    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    // Return the loaded struct
    return result;
  }
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v: {
    // vld3: Load 3-element structure with deinterleaving
    // Loads interleaved data [a0,b0,c0,a1,b1,c1,...] into three vectors
    mlir::Location loc = getLoc(E->getExprLoc());

    // Create struct type: { vec, vec, vec }
    llvm::SmallVector<mlir::Type, 3> members = {vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Call intrinsic: @llvm.aarch64.neon.ld3.<vectype>(ptr)
    llvm::SmallVector<mlir::Type> argTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> args = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld3", structTy, loc);

    // Store result to destination pointer with proper alignment
    // Bitcast pointer to match the struct type
    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    // Return the loaded struct
    return result;
  }
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v: {
    // vld4: Load 4-element structure with deinterleaving
    // Loads interleaved data [a0,b0,c0,d0,a1,b1,c1,d1,...] into four vectors
    mlir::Location loc = getLoc(E->getExprLoc());

    // Create struct type: { vec, vec, vec, vec }
    llvm::SmallVector<mlir::Type, 4> members = {vTy, vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Call intrinsic: @llvm.aarch64.neon.ld4.<vectype>(ptr)
    llvm::SmallVector<mlir::Type> argTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> args = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld4", structTy, loc);

    // Store result to destination pointer with proper alignment
    // Bitcast pointer to match the struct type
    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    // Return the loaded struct
    return result;
  }
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    // vld4_lane: Load 4 elements from memory and insert into specified lane
    // Intrinsic: @llvm.aarch64.neon.ld4lane(vec0, vec1, vec2, vec3, lane, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Create struct type for return value: { vec, vec, vec, vec }
    llvm::SmallVector<mlir::Type, 4> members = {vTy, vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Ops layout: [dest_ptr, src_ptr, vec0, vec1, vec2, vec3, lane]
    // Need to call: ld4lane(vec0, vec1, vec2, vec3, lane_i64, src_ptr)

    // Bitcast the 4 input vectors to vTy
    llvm::SmallVector<mlir::Value> args;
    args.push_back(builder.createBitcast(Ops[2], vTy));
    args.push_back(builder.createBitcast(Ops[3], vTy));
    args.push_back(builder.createBitcast(Ops[4], vTy));
    args.push_back(builder.createBitcast(Ops[5], vTy));

    // Extend lane index to i64
    auto i64Ty = builder.getSInt64Ty();
    args.push_back(builder.createIntCast(Ops[6], i64Ty));

    // Add source pointer
    args.push_back(Ops[1]);

    // Prepare argument types for intrinsic
    llvm::SmallVector<mlir::Type> argTypes(args.size());
    for (size_t i = 0; i < args.size(); ++i)
      argTypes[i] = args[i].getType();

    // Call intrinsic
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld4lane", structTy,
                                            loc);

    // Store result to destination pointer with proper alignment
    // Bitcast pointer to match the struct type
    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    // Return the result struct
    return result;
  }
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v: {
    // vld2_dup: Load one element and duplicate into all lanes of 2 vectors
    mlir::Location loc = getLoc(E->getExprLoc());

    llvm::SmallVector<mlir::Type, 2> members = {vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Call intrinsic: @llvm.aarch64.neon.ld2r.<vectype>(ptr)
    llvm::SmallVector<mlir::Type> argTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> args = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld2r", structTy, loc);

    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    return result;
  }
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v: {
    // vld3_dup: Load one element and duplicate into all lanes of 3 vectors
    mlir::Location loc = getLoc(E->getExprLoc());

    llvm::SmallVector<mlir::Type, 3> members = {vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Call intrinsic: @llvm.aarch64.neon.ld3r.<vectype>(ptr)
    llvm::SmallVector<mlir::Type> argTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> args = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld3r", structTy, loc);

    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    return result;
  }
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    // vld4_dup: Load one element and duplicate into all lanes of 4 vectors
    mlir::Location loc = getLoc(E->getExprLoc());

    llvm::SmallVector<mlir::Type, 4> members = {vTy, vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Call intrinsic: @llvm.aarch64.neon.ld4r.<vectype>(ptr)
    llvm::SmallVector<mlir::Type> argTypes = {Ops[1].getType()};
    llvm::SmallVector<mlir::Value> args = {Ops[1]};
    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld4r", structTy, loc);

    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());

    return result;
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v: {
    // vld2_lane: Load 2 elements from memory and insert into specified lane
    // Intrinsic: @llvm.aarch64.neon.ld2lane(vec0, vec1, lane, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    llvm::SmallVector<mlir::Type, 2> members = {vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Ops layout: [dest_ptr, src_ptr, vec0, vec1, lane]
    llvm::SmallVector<mlir::Value> args;
    args.push_back(builder.createBitcast(Ops[2], vTy));
    args.push_back(builder.createBitcast(Ops[3], vTy));

    auto i64Ty = builder.getSInt64Ty();
    args.push_back(builder.createIntCast(Ops[4], i64Ty));
    args.push_back(Ops[1]);

    llvm::SmallVector<mlir::Type> argTypes(args.size());
    for (size_t i = 0; i < args.size(); ++i)
      argTypes[i] = args[i].getType();

    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld2lane", structTy,
                                            loc);

    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());
    return result;
  }
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v: {
    // vld3_lane: Load 3 elements from memory and insert into specified lane
    // Intrinsic: @llvm.aarch64.neon.ld3lane(vec0, vec1, vec2, lane, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    llvm::SmallVector<mlir::Type, 3> members = {vTy, vTy, vTy};
    auto structTy = cir::RecordType::get(builder.getContext(), members,
                                         /*packed=*/false, /*padded=*/false,
                                         cir::RecordType::Struct);

    // Ops layout: [dest_ptr, src_ptr, vec0, vec1, vec2, lane]
    llvm::SmallVector<mlir::Value> args;
    args.push_back(builder.createBitcast(Ops[2], vTy));
    args.push_back(builder.createBitcast(Ops[3], vTy));
    args.push_back(builder.createBitcast(Ops[4], vTy));

    auto i64Ty = builder.getSInt64Ty();
    args.push_back(builder.createIntCast(Ops[5], i64Ty));
    args.push_back(Ops[1]);

    llvm::SmallVector<mlir::Type> argTypes(args.size());
    for (size_t i = 0; i < args.size(); ++i)
      argTypes[i] = args[i].getType();

    mlir::Value result = emitNeonCallCached(*this, std::move(argTypes), args,
                                            "aarch64.neon.ld3lane", structTy,
                                            loc);

    Address storeAddr = PtrOp0.withElementType(builder, structTy);
    builder.createAlignedStore(loc, result, storeAddr.getPointer(),
                               PtrOp0.getAlignment());
    return result;
  }
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v: {
    // vst2: Store 2-element structure with interleaving
    // Intrinsic: @llvm.aarch64.neon.st2(vec0, vec1, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Ops layout after argument processing: [ptr, vec0, vec1]
    // The struct has been unpacked into individual vectors
    assert(Ops.size() >= 3 && "Expected at least 3 operands for vst2");

    // Bitcast vectors to vTy
    mlir::Value vec0 = builder.createBitcast(Ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(Ops[2], vTy);

    // Call intrinsic: st2(vec0, vec1, ptr)
    llvm::SmallVector<mlir::Type> argTypes(3);
    argTypes[0] = vec0.getType();
    argTypes[1] = vec1.getType();
    argTypes[2] = Ops[0].getType();

    llvm::SmallVector<mlir::Value> args = {vec0, vec1, Ops[0]};

    return emitNeonCallCached(*this, std::move(argTypes), args,
                              "aarch64.neon.st2", builder.getVoidTy(), loc);
  }
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v: {
    // vst2_lane: Store 2 elements from specified lane to memory
    // Intrinsic: @llvm.aarch64.neon.st2lane(vec0, vec1, lane, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Ops layout after argument processing: [ptr, vec0, vec1, lane]
    assert(Ops.size() >= 4 && "Expected at least 4 operands for vst2_lane");

    // Bitcast the 2 vectors to vTy
    mlir::Value vec0 = builder.createBitcast(Ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(Ops[2], vTy);

    // Extend lane index to i64
    auto i64Ty = builder.getSInt64Ty();
    mlir::Value lane = builder.createIntCast(Ops[3], i64Ty);

    // Call intrinsic: st2lane(vec0, vec1, lane_i64, ptr)
    llvm::SmallVector<mlir::Type> argTypes(4);
    argTypes[0] = vec0.getType();
    argTypes[1] = vec1.getType();
    argTypes[2] = lane.getType();
    argTypes[3] = Ops[0].getType();

    llvm::SmallVector<mlir::Value> args = {vec0, vec1, lane, Ops[0]};

    return emitNeonCallCached(*this, std::move(argTypes), args,
                              "aarch64.neon.st2lane", builder.getVoidTy(),
                              loc);
  }
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v: {
    // vst3: Store 3-element structure with interleaving
    // Intrinsic: @llvm.aarch64.neon.st3(vec0, vec1, vec2, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Ops layout after argument processing: [ptr, vec0, vec1, vec2]
    assert(Ops.size() >= 4 && "Expected at least 4 operands for vst3");

    // Bitcast vectors to vTy
    mlir::Value vec0 = builder.createBitcast(Ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(Ops[2], vTy);
    mlir::Value vec2 = builder.createBitcast(Ops[3], vTy);

    // Call intrinsic: st3(vec0, vec1, vec2, ptr)
    llvm::SmallVector<mlir::Type> argTypes(4);
    argTypes[0] = vec0.getType();
    argTypes[1] = vec1.getType();
    argTypes[2] = vec2.getType();
    argTypes[3] = Ops[0].getType();

    llvm::SmallVector<mlir::Value> args = {vec0, vec1, vec2, Ops[0]};

    return emitNeonCallCached(*this, std::move(argTypes), args,
                              "aarch64.neon.st3", builder.getVoidTy(), loc);
  }
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v: {
    // vst3_lane: Store 3 elements from specified lane to memory
    // Intrinsic: @llvm.aarch64.neon.st3lane(vec0, vec1, vec2, lane, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Ops layout after argument processing: [ptr, vec0, vec1, vec2, lane]
    assert(Ops.size() >= 5 && "Expected at least 5 operands for vst3_lane");

    // Bitcast the 3 vectors to vTy
    mlir::Value vec0 = builder.createBitcast(Ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(Ops[2], vTy);
    mlir::Value vec2 = builder.createBitcast(Ops[3], vTy);

    // Extend lane index to i64
    auto i64Ty = builder.getSInt64Ty();
    mlir::Value lane = builder.createIntCast(Ops[4], i64Ty);

    // Call intrinsic: st3lane(vec0, vec1, vec2, lane_i64, ptr)
    llvm::SmallVector<mlir::Type> argTypes(5);
    argTypes[0] = vec0.getType();
    argTypes[1] = vec1.getType();
    argTypes[2] = vec2.getType();
    argTypes[3] = lane.getType();
    argTypes[4] = Ops[0].getType();

    llvm::SmallVector<mlir::Value> args = {vec0, vec1, vec2, lane, Ops[0]};

    return emitNeonCallCached(*this, std::move(argTypes), args,
                              "aarch64.neon.st3lane", builder.getVoidTy(),
                              loc);
  }
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v: {
    // vst4: Store 4-element structure with interleaving
    // Intrinsic: @llvm.aarch64.neon.st4(vec0, vec1, vec2, vec3, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Ops layout after argument processing: [ptr, vec0, vec1, vec2, vec3]
    assert(Ops.size() >= 5 && "Expected at least 5 operands for vst4");

    // Bitcast vectors to vTy
    mlir::Value vec0 = builder.createBitcast(Ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(Ops[2], vTy);
    mlir::Value vec2 = builder.createBitcast(Ops[3], vTy);
    mlir::Value vec3 = builder.createBitcast(Ops[4], vTy);

    // Call intrinsic: st4(vec0, vec1, vec2, vec3, ptr)
    llvm::SmallVector<mlir::Type> argTypes(5);
    argTypes[0] = vec0.getType();
    argTypes[1] = vec1.getType();
    argTypes[2] = vec2.getType();
    argTypes[3] = vec3.getType();
    argTypes[4] = Ops[0].getType();

    llvm::SmallVector<mlir::Value> args = {vec0, vec1, vec2, vec3, Ops[0]};

    return emitNeonCallCached(*this, std::move(argTypes), args,
                              "aarch64.neon.st4", builder.getVoidTy(), loc);
  }
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    // vst4_lane: Store 4 elements from specified lane to memory
    // Intrinsic: @llvm.aarch64.neon.st4lane(vec0, vec1, vec2, vec3, lane, ptr)
    mlir::Location loc = getLoc(E->getExprLoc());

    // Ops layout after argument processing: [ptr, vec0, vec1, vec2, vec3, lane]
    assert(Ops.size() >= 6 && "Expected at least 6 operands for vst4_lane");

    // Bitcast the 4 vectors to vTy
    mlir::Value vec0 = builder.createBitcast(Ops[1], vTy);
    mlir::Value vec1 = builder.createBitcast(Ops[2], vTy);
    mlir::Value vec2 = builder.createBitcast(Ops[3], vTy);
    mlir::Value vec3 = builder.createBitcast(Ops[4], vTy);

    // Extend lane index to i64
    auto i64Ty = builder.getSInt64Ty();
    mlir::Value lane = builder.createIntCast(Ops[5], i64Ty);

    // Call intrinsic: st4lane(vec0, vec1, vec2, vec3, lane_i64, ptr)
    llvm::SmallVector<mlir::Type> argTypes(6);
    argTypes[0] = vec0.getType();
    argTypes[1] = vec1.getType();
    argTypes[2] = vec2.getType();
    argTypes[3] = vec3.getType();
    argTypes[4] = lane.getType();
    argTypes[5] = Ops[0].getType();

    llvm::SmallVector<mlir::Value> args = {vec0, vec1, vec2,
                                           vec3, lane, Ops[0]};

    return emitNeonCallCached(*this, std::move(argTypes), args,
                              "aarch64.neon.st4lane", builder.getVoidTy(),
                              loc);
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    // This set of neon intrinsics implement SIMD matrix transpose.
    // The matrix transposed is always 2x2, and these intrincis transpose
    // multiple 2x2 matrices in parallel, that is why result type is
    // always 2-D matrix whose last dimension is 2.
    // For example `vtrn_s16` would have:
    //  input 1: {0, 1, 2, 3}
    //  input 2; {4, 5, 6, 7}
    //  This basically represents two 2x2 matrices:
    //  [ 0, 1 ]  and  [ 2, 3]
    //  [ 4, 5 ]       [ 6, 7]
    //  They should be simultaneously and independently transposed.
    //  Thus, result is :
    //   { {0, 4, 2, 6},
    //     {1, 5, 3, 7 } }
    Ops[1] = builder.createBitcast(Ops[1], ty);
    Ops[2] = builder.createBitcast(Ops[2], ty);
    // Adding a bitcast here as Ops[0] might be a void pointer.
    mlir::Value baseAddr =
        builder.createBitcast(Ops[0], builder.getPointerTo(ty));
    mlir::Value sv;
    mlir::Location loc = getLoc(E->getExprLoc());

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int64_t, 16> indices;
      for (unsigned i = 0, e = vTy.getSize(); i != e; i += 2) {
        indices.push_back(i + vi);
        indices.push_back(i + e + vi);
      }
      cir::ConstantOp idx = builder.getConstInt(loc, sInt32Ty, vi);
      mlir::Value addr = cir::PtrStrideOp::create(
          builder, loc, baseAddr.getType(), baseAddr, idx,
          cir::GEPNoWrapFlags::inbounds);
      sv = builder.createVecShuffle(loc, Ops[1], Ops[2], indices);
      (void)builder.CIRBaseBuilderTy::createStore(loc, sv, addr);
    }
    return sv;
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[1] = builder.createBitcast(Ops[1], ty);
    Ops[2] = builder.createBitcast(Ops[2], ty);
    // Adding a bitcast here as Ops[0] might be a void pointer.
    mlir::Value baseAddr =
        builder.createBitcast(Ops[0], builder.getPointerTo(ty));
    mlir::Value sv;
    mlir::Location loc = getLoc(E->getExprLoc());

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int64_t, 16> indices;
      for (unsigned i = 0, e = vTy.getSize(); i != e; ++i) {
        indices.push_back(2 * i + vi);
      }
      cir::ConstantOp idx = builder.getConstInt(loc, sInt32Ty, vi);
      mlir::Value addr = cir::PtrStrideOp::create(
          builder, loc, baseAddr.getType(), baseAddr, idx,
          cir::GEPNoWrapFlags::inbounds);
      sv = builder.createVecShuffle(loc, Ops[1], Ops[2], indices);
      (void)builder.CIRBaseBuilderTy::createStore(loc, sv, addr);
    }
    return sv;
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[1] = builder.createBitcast(Ops[1], ty);
    Ops[2] = builder.createBitcast(Ops[2], ty);
    // Adding a bitcast here as Ops[0] might be a void pointer.
    mlir::Value baseAddr =
        builder.createBitcast(Ops[0], builder.getPointerTo(ty));
    mlir::Value sv;
    mlir::Location loc = getLoc(E->getExprLoc());

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int64_t, 16> indices;
      for (unsigned i = 0, e = vTy.getSize(); i != e; i += 2) {
        indices.push_back((i + vi * e) >> 1);
        indices.push_back(((i + vi * e) >> 1) + e);
      }
      cir::ConstantOp idx = builder.getConstInt(loc, sInt32Ty, vi);
      mlir::Value addr = cir::PtrStrideOp::create(
          builder, loc, baseAddr.getType(), baseAddr, idx,
          cir::GEPNoWrapFlags::inbounds);
      sv = builder.createVecShuffle(loc, Ops[1], Ops[2], indices);
      (void)builder.CIRBaseBuilderTy::createStore(loc, sv, addr);
    }
    return sv;
  }
  case NEON::BI__builtin_neon_vqtbl1q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbl1", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbl2q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbl2", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbl3q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbl3", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbl4q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbl4", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbx1q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbx1", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbx2q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbx2", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbx3q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbx3", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vqtbx4q_v:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.tbx4", ty,
                              getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vluti2_lane_mf8:
  case NEON::BI__builtin_neon_vluti2q_lane_mf8:
  case NEON::BI__builtin_neon_vluti2_laneq_mf8:
  case NEON::BI__builtin_neon_vluti2q_laneq_mf8: {
    llvm::StringRef intrinsicName =
        (BuiltinID == NEON::BI__builtin_neon_vluti2_laneq_mf8 ||
         BuiltinID == NEON::BI__builtin_neon_vluti2q_laneq_mf8)
            ? "aarch64.neon.vluti2.laneq"
            : "aarch64.neon.vluti2.lane";
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(E->getExprLoc()),
               cgm.getStringAttrCached(intrinsicName), ty, Ops)
        .getResult();
  }
  case NEON::BI__builtin_neon_vluti4q_lane_mf8:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.vluti4q.lane",
                              ty, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vluti4q_laneq_mf8:
    return emitNeonCallCached(*this, {ty}, Ops, "aarch64.neon.vluti4q.laneq",
                              ty, getLoc(E->getExprLoc()));
  case NEON::BI__builtin_neon_vsqadd_v:
  case NEON::BI__builtin_neon_vsqaddq_v: {
    return emitNeonCallCached(*this, {ty, ty}, Ops, "aarch64.neon.usqadd", ty,
                              getLoc(E->getExprLoc()));
  }
  case NEON::BI__builtin_neon_vuqadd_v:
  case NEON::BI__builtin_neon_vuqaddq_v: {
    return emitNeonCallCached(*this, {ty, ty}, Ops, "aarch64.neon.suqadd", ty,
                              getLoc(E->getExprLoc()));
  }
  }
}
