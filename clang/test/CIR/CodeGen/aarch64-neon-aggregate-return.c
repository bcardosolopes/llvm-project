// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that target-specific builtins that return aggregate types
// (e.g. NEON multi-vector loads) work correctly through CIR.

#include <arm_neon.h>

// CIR-LABEL: define {{.*}} @test_vld2q
// OGCG-LABEL: define {{.*}} @test_vld2q
uint32x4x2_t test_vld2q(const uint32_t *ptr) {
  return vld2q_u32(ptr);
}
// CIR: call { <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld2.v4i32
// OGCG: call { <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld2.v4i32

// CIR-LABEL: define {{.*}} @test_vld3q
// OGCG-LABEL: define {{.*}} @test_vld3q
float32x4x3_t test_vld3q(const float *ptr) {
  return vld3q_f32(ptr);
}
// CIR: call { <4 x float>, <4 x float>, <4 x float> } @llvm.aarch64.neon.ld3.v4f32
// OGCG: call { <4 x float>, <4 x float>, <4 x float> } @llvm.aarch64.neon.ld3.v4f32
