// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vld1q_x2_f32
// CIR: call { <4 x float>, <4 x float> } @llvm.aarch64.neon.ld1x2.v4f32.p0
// OGCG-LABEL: @test_vld1q_x2_f32
// OGCG: call { <4 x float>, <4 x float> } @llvm.aarch64.neon.ld1x2.v4f32.p0
float32x4x2_t test_vld1q_x2_f32(const float *ptr) {
  return vld1q_f32_x2(ptr);
}

// CIR-LABEL: @test_vld1q_x3_f32
// CIR: call { <4 x float>, <4 x float>, <4 x float> } @llvm.aarch64.neon.ld1x3.v4f32.p0
// OGCG-LABEL: @test_vld1q_x3_f32
// OGCG: call { <4 x float>, <4 x float>, <4 x float> } @llvm.aarch64.neon.ld1x3.v4f32.p0
float32x4x3_t test_vld1q_x3_f32(const float *ptr) {
  return vld1q_f32_x3(ptr);
}

// CIR-LABEL: @test_vld1q_x4_f32
// CIR: call { <4 x float>, <4 x float>, <4 x float>, <4 x float> } @llvm.aarch64.neon.ld1x4.v4f32.p0
// OGCG-LABEL: @test_vld1q_x4_f32
// OGCG: call { <4 x float>, <4 x float>, <4 x float>, <4 x float> } @llvm.aarch64.neon.ld1x4.v4f32.p0
float32x4x4_t test_vld1q_x4_f32(const float *ptr) {
  return vld1q_f32_x4(ptr);
}

// CIR-LABEL: @test_vst1q_x2_f32
// CIR: call void @llvm.aarch64.neon.st1x2.v4f32.p0(<4 x float>
// OGCG-LABEL: @test_vst1q_x2_f32
// OGCG: call void @llvm.aarch64.neon.st1x2.v4f32.p0(<4 x float>
void test_vst1q_x2_f32(float *ptr, float32x4x2_t src) {
  vst1q_f32_x2(ptr, src);
}

// CIR-LABEL: @test_vst1q_x3_f32
// CIR: call void @llvm.aarch64.neon.st1x3.v4f32.p0(<4 x float>
// OGCG-LABEL: @test_vst1q_x3_f32
// OGCG: call void @llvm.aarch64.neon.st1x3.v4f32.p0(<4 x float>
void test_vst1q_x3_f32(float *ptr, float32x4x3_t src) {
  vst1q_f32_x3(ptr, src);
}

// CIR-LABEL: @test_vst1q_x4_f32
// CIR: call void @llvm.aarch64.neon.st1x4.v4f32.p0(<4 x float>
// OGCG-LABEL: @test_vst1q_x4_f32
// OGCG: call void @llvm.aarch64.neon.st1x4.v4f32.p0(<4 x float>
void test_vst1q_x4_f32(float *ptr, float32x4x4_t src) {
  vst1q_f32_x4(ptr, src);
}
