// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vpmin_f32
// CIR: call <2 x float> @llvm.aarch64.neon.fminp.v2f32(<2 x float>
// OGCG-LABEL: @test_vpmin_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fminp.v2f32(<2 x float>
float32x2_t test_vpmin_f32(float32x2_t a, float32x2_t b) {
  return vpmin_f32(a, b);
}

// CIR-LABEL: @test_vpminq_f32
// CIR: call <4 x float> @llvm.aarch64.neon.fminp.v4f32(<4 x float>
// OGCG-LABEL: @test_vpminq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fminp.v4f32(<4 x float>
float32x4_t test_vpminq_f32(float32x4_t a, float32x4_t b) {
  return vpminq_f32(a, b);
}

// CIR-LABEL: @test_vpmax_f32
// CIR: call <2 x float> @llvm.aarch64.neon.fmaxp.v2f32(<2 x float>
// OGCG-LABEL: @test_vpmax_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fmaxp.v2f32(<2 x float>
float32x2_t test_vpmax_f32(float32x2_t a, float32x2_t b) {
  return vpmax_f32(a, b);
}

// CIR-LABEL: @test_vpmaxq_f32
// CIR: call <4 x float> @llvm.aarch64.neon.fmaxp.v4f32(<4 x float>
// OGCG-LABEL: @test_vpmaxq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fmaxp.v4f32(<4 x float>
float32x4_t test_vpmaxq_f32(float32x4_t a, float32x4_t b) {
  return vpmaxq_f32(a, b);
}

// CIR-LABEL: @test_vpmin_s32
// CIR: call <2 x i32> @llvm.aarch64.neon.sminp.v2i32(<2 x i32>
// OGCG-LABEL: @test_vpmin_s32
// OGCG: call <2 x i32> @llvm.aarch64.neon.sminp.v2i32(<2 x i32>
int32x2_t test_vpmin_s32(int32x2_t a, int32x2_t b) {
  return vpmin_s32(a, b);
}

// CIR-LABEL: @test_vpmin_u32
// CIR: call <2 x i32> @llvm.aarch64.neon.uminp.v2i32(<2 x i32>
// OGCG-LABEL: @test_vpmin_u32
// OGCG: call <2 x i32> @llvm.aarch64.neon.uminp.v2i32(<2 x i32>
uint32x2_t test_vpmin_u32(uint32x2_t a, uint32x2_t b) {
  return vpmin_u32(a, b);
}

// CIR-LABEL: @test_vpmax_s32
// CIR: call <2 x i32> @llvm.aarch64.neon.smaxp.v2i32(<2 x i32>
// OGCG-LABEL: @test_vpmax_s32
// OGCG: call <2 x i32> @llvm.aarch64.neon.smaxp.v2i32(<2 x i32>
int32x2_t test_vpmax_s32(int32x2_t a, int32x2_t b) {
  return vpmax_s32(a, b);
}

// CIR-LABEL: @test_vpmax_u32
// CIR: call <2 x i32> @llvm.aarch64.neon.umaxp.v2i32(<2 x i32>
// OGCG-LABEL: @test_vpmax_u32
// OGCG: call <2 x i32> @llvm.aarch64.neon.umaxp.v2i32(<2 x i32>
uint32x2_t test_vpmax_u32(uint32x2_t a, uint32x2_t b) {
  return vpmax_u32(a, b);
}
