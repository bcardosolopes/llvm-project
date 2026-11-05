// REQUIRES: aarch64-registered-target
// Mixed-sign (unsigned x signed) i8 NEON builtins used in quantized ML: their
// i8 operands must stay <16 x i8> (their natural int8x16 type). Bitcasting
// them to the i32 result type spells a non-existent usmmla.v4i32.v4i32 /
// usdot.v4i32.v4i32 overload that the backend cannot select.

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +i8mm \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +i8mm \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// --- vusmmlaq: mixed-sign i8 matrix multiply-accumulate ---

// CIR-LABEL: @test_vusmmlaq_s32
// CIR: call <4 x i32> @llvm.aarch64.neon.usmmla.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
// OGCG-LABEL: @test_vusmmlaq_s32
// OGCG: call <4 x i32> @llvm.aarch64.neon.usmmla.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
int32x4_t test_vusmmlaq_s32(int32x4_t r, uint8x16_t a, int8x16_t b) {
  return vusmmlaq_s32(r, a, b);
}

// --- vusdot: mixed-sign i8 dot-product accumulate ---

// CIR-LABEL: @test_vusdotq_s32
// CIR: call <4 x i32> @llvm.aarch64.neon.usdot.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
// OGCG-LABEL: @test_vusdotq_s32
// OGCG: call <4 x i32> @llvm.aarch64.neon.usdot.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
int32x4_t test_vusdotq_s32(int32x4_t r, uint8x16_t a, int8x16_t b) {
  return vusdotq_s32(r, a, b);
}

// CIR-LABEL: @test_vusdot_s32
// CIR: call <2 x i32> @llvm.aarch64.neon.usdot.v2i32.v8i8(<2 x i32> %{{.*}}, <8 x i8> %{{.*}}, <8 x i8>
// OGCG-LABEL: @test_vusdot_s32
// OGCG: call <2 x i32> @llvm.aarch64.neon.usdot.v2i32.v8i8(<2 x i32> %{{.*}}, <8 x i8> %{{.*}}, <8 x i8>
int32x2_t test_vusdot_s32(int32x2_t r, uint8x8_t a, int8x8_t b) {
  return vusdot_s32(r, a, b);
}

// --- lane-indexed mixed-sign dot product (wraps the base vusdot builtin) ---

// CIR-LABEL: @test_vusdotq_lane_s32
// CIR: call <4 x i32> @llvm.aarch64.neon.usdot.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
// OGCG-LABEL: @test_vusdotq_lane_s32
// OGCG: call <4 x i32> @llvm.aarch64.neon.usdot.v4i32.v16i8(<4 x i32> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8>
int32x4_t test_vusdotq_lane_s32(int32x4_t r, uint8x16_t a, int8x8_t b) {
  return vusdotq_lane_s32(r, a, b, 0);
}
