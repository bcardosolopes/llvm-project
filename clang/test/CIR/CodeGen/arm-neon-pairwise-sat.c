// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +neon -target-feature +fp16 -fclangir -emit-llvm -O2 -o - %s | FileCheck %s

// AArch32 NEON pairwise min/max, halving-subtract, and saturating abs/neg +
// count-leading-sign. Previously NYI on 32-bit ARM (emitARMBuiltinExpr);
// now lowered to the same arm.neon.* intrinsics as classic CodeGen. Note
// float pairwise uses the SIGNED intrinsic name (vpmaxs) since NeonTypeFlags
// reports float as non-unsigned.

#include <arm_neon.h>

// CHECK-LABEL: @pmax_s(
// CHECK: call <2 x i32> @llvm.arm.neon.vpmaxs.v2i32(
int32x2_t pmax_s(int32x2_t a, int32x2_t b) { return vpmax_s32(a, b); }

// CHECK-LABEL: @pmax_u(
// CHECK: call <2 x i32> @llvm.arm.neon.vpmaxu.v2i32(
uint32x2_t pmax_u(uint32x2_t a, uint32x2_t b) { return vpmax_u32(a, b); }

// CHECK-LABEL: @pmax_f(
// CHECK: call <2 x float> @llvm.arm.neon.vpmaxs.v2f32(
float32x2_t pmax_f(float32x2_t a, float32x2_t b) { return vpmax_f32(a, b); }

// CHECK-LABEL: @pmin_u(
// CHECK: call <8 x i8> @llvm.arm.neon.vpminu.v8i8(
uint8x8_t pmin_u(uint8x8_t a, uint8x8_t b) { return vpmin_u8(a, b); }

// CHECK-LABEL: @hsub_s(
// CHECK: call <4 x i32> @llvm.arm.neon.vhsubs.v4i32(
int32x4_t hsub_s(int32x4_t a, int32x4_t b) { return vhsubq_s32(a, b); }

// CHECK-LABEL: @qabs(
// CHECK: call <4 x i32> @llvm.arm.neon.vqabs.v4i32(
int32x4_t qabs(int32x4_t a) { return vqabsq_s32(a); }

// CHECK-LABEL: @qneg(
// CHECK: call <8 x i16> @llvm.arm.neon.vqneg.v8i16(
int16x8_t qneg(int16x8_t a) { return vqnegq_s16(a); }

// CHECK-LABEL: @cls(
// CHECK: call <4 x i32> @llvm.arm.neon.vcls.v4i32(
int32x4_t cls(int32x4_t a) { return vclsq_s32(a); }

// CHECK-LABEL: @addhn(
// CHECK: add <8 x i16>
// CHECK: lshr <8 x i16> %{{.*}}, {{splat \(i16 8\)|<i16 8}}
// CHECK: trunc {{.*}}<8 x i16> %{{.*}} to <8 x i8>
int8x8_t addhn(int16x8_t a, int16x8_t b) { return vaddhn_s16(a, b); }

// CHECK-LABEL: @raddhn(
// CHECK: call <8 x i8> @llvm.arm.neon.vraddhn.v8i8(
int8x8_t raddhn(int16x8_t a, int16x8_t b) { return vraddhn_s16(a, b); }

// CHECK-LABEL: @cvt_narrow(
// CHECK: call <4 x i16> @llvm.arm.neon.vcvtfp2hf(<4 x float>
float16x4_t cvt_narrow(float32x4_t a) { return vcvt_f16_f32(a); }

// CHECK-LABEL: @cvt_widen(
// CHECK: call <4 x float> @llvm.arm.neon.vcvthf2fp(<4 x i16>
float32x4_t cvt_widen(float16x4_t a) { return vcvt_f32_f16(a); }

// CHECK-LABEL: @pmul(
// CHECK: call <8 x i8> @llvm.arm.neon.vmulp.v8i8(
poly8x8_t pmul(poly8x8_t a, poly8x8_t b) { return vmul_p8(a, b); }

// CHECK-LABEL: @pmulq(
// CHECK: call <16 x i8> @llvm.arm.neon.vmulp.v16i8(
poly8x16_t pmulq(poly8x16_t a, poly8x16_t b) { return vmulq_p8(a, b); }

// CHECK-LABEL: @qrshl(
// CHECK: call <4 x i32> @llvm.arm.neon.vqrshifts.v4i32(
int32x4_t qrshl(int32x4_t a, int32x4_t b) { return vqrshlq_s32(a, b); }

// CHECK-LABEL: @padd(
// CHECK: xor <8 x i8>
poly8x8_t padd(poly8x8_t a, poly8x8_t b) { return vadd_p8(a, b); }
