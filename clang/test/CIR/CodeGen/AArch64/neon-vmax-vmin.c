// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that vmax/vmin emit aarch64.neon.fmax/fmin (IEEE 754-2008 maxNum/
// minNum, NaN-suppressing) rather than llvm.maximum/minimum (IEEE 754-2019,
// NaN-propagating).

#include <arm_neon.h>

// CIR-LABEL: @test_vmaxq_f32
// CIR: call <4 x float> @llvm.aarch64.neon.fmax.v4f32(
// OGCG-LABEL: @test_vmaxq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fmax.v4f32(
float32x4_t test_vmaxq_f32(float32x4_t a, float32x4_t b) {
  return vmaxq_f32(a, b);
}

// CIR-LABEL: @test_vmax_f32
// CIR: call <2 x float> @llvm.aarch64.neon.fmax.v2f32(
// OGCG-LABEL: @test_vmax_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fmax.v2f32(
float32x2_t test_vmax_f32(float32x2_t a, float32x2_t b) {
  return vmax_f32(a, b);
}

// CIR-LABEL: @test_vminq_f32
// CIR: call <4 x float> @llvm.aarch64.neon.fmin.v4f32(
// OGCG-LABEL: @test_vminq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fmin.v4f32(
float32x4_t test_vminq_f32(float32x4_t a, float32x4_t b) {
  return vminq_f32(a, b);
}

// CIR-LABEL: @test_vmin_f32
// CIR: call <2 x float> @llvm.aarch64.neon.fmin.v2f32(
// OGCG-LABEL: @test_vmin_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fmin.v2f32(
float32x2_t test_vmin_f32(float32x2_t a, float32x2_t b) {
  return vmin_f32(a, b);
}

// CIR-LABEL: @test_vmaxq_s32
// CIR: call <4 x i32> @llvm.aarch64.neon.smax.v4i32(
// OGCG-LABEL: @test_vmaxq_s32
// OGCG: call <4 x i32> @llvm.aarch64.neon.smax.v4i32(
int32x4_t test_vmaxq_s32(int32x4_t a, int32x4_t b) {
  return vmaxq_s32(a, b);
}

// CIR-LABEL: @test_vmaxq_u32
// CIR: call <4 x i32> @llvm.aarch64.neon.umax.v4i32(
// OGCG-LABEL: @test_vmaxq_u32
// OGCG: call <4 x i32> @llvm.aarch64.neon.umax.v4i32(
uint32x4_t test_vmaxq_u32(uint32x4_t a, uint32x4_t b) {
  return vmaxq_u32(a, b);
}

// CIR-LABEL: @test_vminq_s32
// CIR: call <4 x i32> @llvm.aarch64.neon.smin.v4i32(
// OGCG-LABEL: @test_vminq_s32
// OGCG: call <4 x i32> @llvm.aarch64.neon.smin.v4i32(
int32x4_t test_vminq_s32(int32x4_t a, int32x4_t b) {
  return vminq_s32(a, b);
}

// CIR-LABEL: @test_vminq_u8
// CIR: call <16 x i8> @llvm.aarch64.neon.umin.v16i8(
// OGCG-LABEL: @test_vminq_u8
// OGCG: call <16 x i8> @llvm.aarch64.neon.umin.v16i8(
uint8x16_t test_vminq_u8(uint8x16_t a, uint8x16_t b) {
  return vminq_u8(a, b);
}
