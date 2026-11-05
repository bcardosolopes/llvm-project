// REQUIRES: aarch64-registered-target
// Tests for the generic fallthrough path in emitCommonNeonBuiltinExpr that
// handles Add1ArgType modifier for both integer narrowing and float intrinsics.

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +v8.5a -target-feature +complxnum \
// RUN:   -target-feature +frintts \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +v8.5a -target-feature +complxnum \
// RUN:   -target-feature +frintts \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// --- Float Add1ArgType: vcadd (complex add with rotation) ---
// These have Add1ArgType but args match result type (float vectors).

// CIR-LABEL: @test_vcadd_rot90_f32
// CIR: call <2 x float> @llvm.aarch64.neon.vcadd.rot90.v2f32(<2 x float>
// OGCG-LABEL: @test_vcadd_rot90_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.vcadd.rot90.v2f32(<2 x float>
float32x2_t test_vcadd_rot90_f32(float32x2_t a, float32x2_t b) {
  return vcadd_rot90_f32(a, b);
}

// CIR-LABEL: @test_vcaddq_rot270_f32
// CIR: call <4 x float> @llvm.aarch64.neon.vcadd.rot270.v4f32(<4 x float>
// OGCG-LABEL: @test_vcaddq_rot270_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.vcadd.rot270.v4f32(<4 x float>
float32x4_t test_vcaddq_rot270_f32(float32x4_t a, float32x4_t b) {
  return vcaddq_rot270_f32(a, b);
}

// --- Float Add1ArgType: vrnd32/vrnd64 (round to int) ---

// CIR-LABEL: @test_vrnd32x_f32
// CIR: call <2 x float> @llvm.aarch64.neon.frint32x.v2f32(<2 x float>
// OGCG-LABEL: @test_vrnd32x_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.frint32x.v2f32(<2 x float>
float32x2_t test_vrnd32x_f32(float32x2_t a) {
  return vrnd32x_f32(a);
}

// CIR-LABEL: @test_vrnd64zq_f32
// CIR: call <4 x float> @llvm.aarch64.neon.frint64z.v4f32(<4 x float>
// OGCG-LABEL: @test_vrnd64zq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.frint64z.v4f32(<4 x float>
float32x4_t test_vrnd64zq_f32(float32x4_t a) {
  return vrnd64zq_f32(a);
}
