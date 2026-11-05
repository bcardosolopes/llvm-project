// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// NEON rounding intrinsics (vrndm/vrndp/vrndx).

typedef __attribute__((neon_vector_type(4))) float float32x4_t;
typedef __attribute__((neon_vector_type(2))) float float32x2_t;

// vrndm: round toward negative infinity (floor)

// CIR-LABEL: @test_vrndmq
// CIR: call <4 x float> @llvm.floor.v4f32(
float32x4_t test_vrndmq(float32x4_t a) {
  return __builtin_neon_vrndmq_v(a, 41);
}

// CIR-LABEL: @test_vrndm
// CIR: call <2 x float> @llvm.floor.v2f32(
float32x2_t test_vrndm(float32x2_t a) {
  return __builtin_neon_vrndm_v(a, 9);
}

// OGCG-LABEL: @test_vrndmq
// OGCG: call <4 x float> @llvm.floor.v4f32(
// OGCG-LABEL: @test_vrndm
// OGCG: call <2 x float> @llvm.floor.v2f32(

// vrndp: round toward positive infinity (ceil)

// CIR-LABEL: @test_vrndpq
// CIR: call <4 x float> @llvm.ceil.v4f32(
float32x4_t test_vrndpq(float32x4_t a) {
  return __builtin_neon_vrndpq_v(a, 41);
}

// CIR-LABEL: @test_vrndp
// CIR: call <2 x float> @llvm.ceil.v2f32(
float32x2_t test_vrndp(float32x2_t a) {
  return __builtin_neon_vrndp_v(a, 9);
}

// OGCG-LABEL: @test_vrndpq
// OGCG: call <4 x float> @llvm.ceil.v4f32(
// OGCG-LABEL: @test_vrndp
// OGCG: call <2 x float> @llvm.ceil.v2f32(

// vrndx: round to integral using current rounding mode (rint)

// CIR-LABEL: @test_vrndxq
// CIR: call <4 x float> @llvm.rint.v4f32(
float32x4_t test_vrndxq(float32x4_t a) {
  return __builtin_neon_vrndxq_v(a, 41);
}

// CIR-LABEL: @test_vrndx
// CIR: call <2 x float> @llvm.rint.v2f32(
float32x2_t test_vrndx(float32x2_t a) {
  return __builtin_neon_vrndx_v(a, 9);
}

// OGCG-LABEL: @test_vrndxq
// OGCG: call <4 x float> @llvm.rint.v4f32(
// OGCG-LABEL: @test_vrndx
// OGCG: call <2 x float> @llvm.rint.v2f32(
