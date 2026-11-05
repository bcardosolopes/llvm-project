// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

typedef __attribute__((neon_vector_type(4))) float float32x4_t;
typedef __attribute__((neon_vector_type(2))) float float32x2_t;

// CHECK-LABEL: @test_vrecps
// CHECK: call <2 x float> @llvm.aarch64.neon.frecps.v2f32(
float32x2_t test_vrecps(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vrecps_v(a, b, 9); // 9 = float32x2
}

// CHECK-LABEL: @test_vrecpsq
// CHECK: call <4 x float> @llvm.aarch64.neon.frecps.v4f32(
float32x4_t test_vrecpsq(float32x4_t a, float32x4_t b) {
  return __builtin_neon_vrecpsq_v(a, b, 41); // 41 = float32x4
}

// CHECK-LABEL: @test_vrsqrts
// CHECK: call <2 x float> @llvm.aarch64.neon.frsqrts.v2f32(
float32x2_t test_vrsqrts(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vrsqrts_v(a, b, 9); // 9 = float32x2
}

// CHECK-LABEL: @test_vrsqrtsq
// CHECK: call <4 x float> @llvm.aarch64.neon.frsqrts.v4f32(
float32x4_t test_vrsqrtsq(float32x4_t a, float32x4_t b) {
  return __builtin_neon_vrsqrtsq_v(a, b, 41); // 41 = float32x4
}

// CHECK-LABEL: @test_vrsqrte
// CHECK: call <2 x float> @llvm.aarch64.neon.frsqrte.v2f32(
float32x2_t test_vrsqrte(float32x2_t a) {
  return __builtin_neon_vrsqrte_v(a, 9);
}

// CHECK-LABEL: @test_vrsqrteq
// CHECK: call <4 x float> @llvm.aarch64.neon.frsqrte.v4f32(
float32x4_t test_vrsqrteq(float32x4_t a) {
  return __builtin_neon_vrsqrteq_v(a, 41);
}
