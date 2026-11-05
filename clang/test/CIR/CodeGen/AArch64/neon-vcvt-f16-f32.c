// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

#include <arm_neon.h>

float32x4_t test_vcvt_f32_f16(float16x4_t a) {
  return vcvt_f32_f16(a);
}
// CIR-LABEL: @test_vcvt_f32_f16
// CIR: call <4 x float> @llvm.aarch64.neon.vcvthf2fp(<4 x i16>
// OGCG-LABEL: @test_vcvt_f32_f16
// OGCG: call <4 x float> @llvm.aarch64.neon.vcvthf2fp(<4 x i16>

float16x4_t test_vcvt_f16_f32(float32x4_t a) {
  return vcvt_f16_f32(a);
}
// CIR-LABEL: @test_vcvt_f16_f32
// CIR: call <4 x i16> @llvm.aarch64.neon.vcvtfp2hf(<4 x float>
// OGCG-LABEL: @test_vcvt_f16_f32
// OGCG: call <4 x i16> @llvm.aarch64.neon.vcvtfp2hf(<4 x float>
