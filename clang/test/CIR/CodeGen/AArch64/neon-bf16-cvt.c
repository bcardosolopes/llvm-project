// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +bf16 \
// RUN:   -target-feature +neon -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +bf16 \
// RUN:   -target-feature +neon -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

#include <arm_neon.h>

// CIR-LABEL: @test_vcvth_bf16(
// CIR: fptrunc float {{%.*}} to bfloat
//
// OGCG-LABEL: @test_vcvth_bf16(
// OGCG: fptrunc float {{%.*}} to bfloat
bfloat16_t test_vcvth_bf16(float32_t a) {
  return vcvth_bf16_f32(a);
}

// CIR-LABEL: @test_vcvt_bf16(
// CIR: fptrunc <4 x float> {{%.*}} to <4 x bfloat>
//
// OGCG-LABEL: @test_vcvt_bf16(
// OGCG: fptrunc <4 x float> {{%.*}} to <4 x bfloat>
bfloat16x4_t test_vcvt_bf16(float32x4_t a) {
  return vcvt_bf16_f32(a);
}

// CIR-LABEL: @test_vcvtq_low_bf16(
// CIR: fptrunc <4 x float> {{%.*}} to <4 x bfloat>
// CIR: shufflevector <4 x bfloat> {{%.*}}, <4 x bfloat> zeroinitializer, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
//
// OGCG-LABEL: @test_vcvtq_low_bf16(
// OGCG: fptrunc <4 x float> {{%.*}} to <4 x bfloat>
// OGCG: shufflevector <4 x bfloat> {{%.*}}, <4 x bfloat> zeroinitializer, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
bfloat16x8_t test_vcvtq_low_bf16(float32x4_t a) {
  return vcvtq_low_bf16_f32(a);
}

// CIR-LABEL: @test_vcvtq_high_bf16(
// CIR: shufflevector <8 x bfloat> {{%.*}}, <8 x bfloat> {{%.*}}, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
// CIR: fptrunc <4 x float> {{%.*}} to <4 x bfloat>
// CIR: shufflevector <4 x bfloat> {{%.*}}, <4 x bfloat> {{%.*}}, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
//
// OGCG-LABEL: @test_vcvtq_high_bf16(
// OGCG: shufflevector <8 x bfloat> {{%.*}}, <8 x bfloat> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
// OGCG: fptrunc <4 x float> {{%.*}} to <4 x bfloat>
// OGCG: shufflevector <4 x bfloat> {{%.*}}, <4 x bfloat> {{%.*}}, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
bfloat16x8_t test_vcvtq_high_bf16(bfloat16x8_t inactive, float32x4_t a) {
  return vcvtq_high_bf16_f32(inactive, a);
}
