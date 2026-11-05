// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vqdmulh_laneq_s16_0(
// CIR: call <4 x i16> @llvm.aarch64.neon.sqdmulh.laneq.v4i16.v8i16(<4 x i16>
// CIR-SAME: <8 x i16>
// CIR-SAME: i32 0)
// OGCG-LABEL: @test_vqdmulh_laneq_s16_0(
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqdmulh.laneq.v4i16.v8i16(<4 x i16>
// OGCG-SAME: <8 x i16>
// OGCG-SAME: i32 0)
int16x4_t test_vqdmulh_laneq_s16_0(int16x4_t a, int16x8_t v) {
  return vqdmulh_laneq_s16(a, v, 0);
}

// CIR-LABEL: @test_vqdmulhq_laneq_s16_0(
// CIR: call <8 x i16> @llvm.aarch64.neon.sqdmulh.laneq.v8i16.v8i16(<8 x i16>
// CIR-SAME: <8 x i16>
// CIR-SAME: i32 0)
// OGCG-LABEL: @test_vqdmulhq_laneq_s16_0(
// OGCG: call <8 x i16> @llvm.aarch64.neon.sqdmulh.laneq.v8i16.v8i16(<8 x i16>
// OGCG-SAME: <8 x i16>
// OGCG-SAME: i32 0)
int16x8_t test_vqdmulhq_laneq_s16_0(int16x8_t a, int16x8_t v) {
  return vqdmulhq_laneq_s16(a, v, 0);
}

// CIR-LABEL: @test_vqrdmulh_laneq_s32_0(
// CIR: call <2 x i32> @llvm.aarch64.neon.sqrdmulh.laneq.v2i32.v4i32(<2 x i32>
// CIR-SAME: <4 x i32>
// CIR-SAME: i32 0)
// OGCG-LABEL: @test_vqrdmulh_laneq_s32_0(
// OGCG: call <2 x i32> @llvm.aarch64.neon.sqrdmulh.laneq.v2i32.v4i32(<2 x i32>
// OGCG-SAME: <4 x i32>
// OGCG-SAME: i32 0)
int32x2_t test_vqrdmulh_laneq_s32_0(int32x2_t a, int32x4_t v) {
  return vqrdmulh_laneq_s32(a, v, 0);
}

// CIR-LABEL: @test_vqrdmulhq_laneq_s32_0(
// CIR: call <4 x i32> @llvm.aarch64.neon.sqrdmulh.laneq.v4i32.v4i32(<4 x i32>
// CIR-SAME: <4 x i32>
// CIR-SAME: i32 0)
// OGCG-LABEL: @test_vqrdmulhq_laneq_s32_0(
// OGCG: call <4 x i32> @llvm.aarch64.neon.sqrdmulh.laneq.v4i32.v4i32(<4 x i32>
// OGCG-SAME: <4 x i32>
// OGCG-SAME: i32 0)
int32x4_t test_vqrdmulhq_laneq_s32_0(int32x4_t a, int32x4_t v) {
  return vqrdmulhq_laneq_s32(a, v, 0);
}
