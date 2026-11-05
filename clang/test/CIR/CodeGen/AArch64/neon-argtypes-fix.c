// REQUIRES: aarch64-registered-target
// Tests for emitNeonCall argTypes fixes: vqdmull, vcvt_n, vmmlaq

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +i8mm \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +i8mm \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// --- vqdmull: saturating doubling multiply long ---

// CIR-LABEL: @test_vqdmull_s16
// CIR: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
// OGCG-LABEL: @test_vqdmull_s16
// OGCG: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
int32x4_t test_vqdmull_s16(int16x4_t a, int16x4_t b) {
  return vqdmull_s16(a, b);
}

// CIR-LABEL: @test_vqdmull_s32
// CIR: call <2 x i64> @llvm.aarch64.neon.sqdmull.v2i64(<2 x i32>
// OGCG-LABEL: @test_vqdmull_s32
// OGCG: call <2 x i64> @llvm.aarch64.neon.sqdmull.v2i64(<2 x i32>
int64x2_t test_vqdmull_s32(int32x2_t a, int32x2_t b) {
  return vqdmull_s32(a, b);
}

// --- vcvt_n: int-to-float fixed-point conversion ---

// CIR-LABEL: @test_vcvt_n_f32_s32
// CIR: call <2 x float> @llvm.aarch64.neon.vcvtfxs2fp.v2f32.v2i32(<2 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvt_n_f32_s32
// OGCG: call <2 x float> @llvm.aarch64.neon.vcvtfxs2fp.v2f32.v2i32(<2 x i32> %{{.*}}, i32 3)
float32x2_t test_vcvt_n_f32_s32(int32x2_t a) {
  return vcvt_n_f32_s32(a, 3);
}

// CIR-LABEL: @test_vcvtq_n_f64_u64
// CIR: call <2 x double> @llvm.aarch64.neon.vcvtfxu2fp.v2f64.v2i64(<2 x i64> %{{.*}}, i32 5)
// OGCG-LABEL: @test_vcvtq_n_f64_u64
// OGCG: call <2 x double> @llvm.aarch64.neon.vcvtfxu2fp.v2f64.v2i64(<2 x i64> %{{.*}}, i32 5)
float64x2_t test_vcvtq_n_f64_u64(uint64x2_t a) {
  return vcvtq_n_f64_u64(a, 5);
}

// --- vcvt_n: float-to-fixed-point conversion ---

// CIR-LABEL: @test_vcvt_n_s32_f32
// CIR: call <2 x i32> @llvm.aarch64.neon.vcvtfp2fxs.v2i32.v2f32(<2 x float> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvt_n_s32_f32
// OGCG: call <2 x i32> @llvm.aarch64.neon.vcvtfp2fxs.v2i32.v2f32(<2 x float> %{{.*}}, i32 3)
int32x2_t test_vcvt_n_s32_f32(float32x2_t a) {
  return vcvt_n_s32_f32(a, 3);
}

// CIR-LABEL: @test_vcvtq_n_u32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.vcvtfp2fxu.v4i32.v4f32(<4 x float> %{{.*}}, i32 5)
// OGCG-LABEL: @test_vcvtq_n_u32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.vcvtfp2fxu.v4i32.v4f32(<4 x float> %{{.*}}, i32 5)
uint32x4_t test_vcvtq_n_u32_f32(float32x4_t a) {
  return vcvtq_n_u32_f32(a, 5);
}

// CIR-LABEL: @test_vcvtq_n_s64_f64
// CIR: call <2 x i64> @llvm.aarch64.neon.vcvtfp2fxs.v2i64.v2f64(<2 x double> %{{.*}}, i32 7)
// OGCG-LABEL: @test_vcvtq_n_s64_f64
// OGCG: call <2 x i64> @llvm.aarch64.neon.vcvtfp2fxs.v2i64.v2f64(<2 x double> %{{.*}}, i32 7)
int64x2_t test_vcvtq_n_s64_f64(float64x2_t a) {
  return vcvtq_n_s64_f64(a, 7);
}

// --- vmmlaq: matrix multiply-accumulate ---

// CIR-LABEL: @test_vmmlaq_s32
// CIR: call <4 x i32> @llvm.aarch64.neon.smmla.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
// OGCG-LABEL: @test_vmmlaq_s32
// OGCG: call <4 x i32> @llvm.aarch64.neon.smmla.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
int32x4_t test_vmmlaq_s32(int32x4_t r, int8x16_t a, int8x16_t b) {
  return vmmlaq_s32(r, a, b);
}

// CIR-LABEL: @test_vmmlaq_u32
// CIR: call <4 x i32> @llvm.aarch64.neon.ummla.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
// OGCG-LABEL: @test_vmmlaq_u32
// OGCG: call <4 x i32> @llvm.aarch64.neon.ummla.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
uint32x4_t test_vmmlaq_u32(uint32x4_t r, uint8x16_t a, uint8x16_t b) {
  return vmmlaq_u32(r, a, b);
}
