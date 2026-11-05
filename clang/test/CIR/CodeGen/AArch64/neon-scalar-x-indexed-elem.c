// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vqdmlalh_lane_s16
// CIR: extractelement <4 x i16>
// CIR: insertelement <4 x i16> poison, i16 %{{.*}}, i64 0
// CIR: insertelement <4 x i16> poison, i16 %{{.*}}, i64 0
// CIR: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
// CIR: extractelement <4 x i32>
// CIR: call i32 @llvm.aarch64.neon.sqadd.i32(i32
// OGCG-LABEL: @test_vqdmlalh_lane_s16
// OGCG: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
// OGCG: call i32 @llvm.aarch64.neon.sqadd.i32(i32
int32_t test_vqdmlalh_lane_s16(int32_t a, int16_t b, int16x4_t c) {
  return vqdmlalh_lane_s16(a, b, c, 3);
}

// CIR-LABEL: @test_vqdmlslh_laneq_s16
// CIR: extractelement <8 x i16>
// CIR: insertelement <4 x i16> poison, i16 %{{.*}}, i64 0
// CIR: insertelement <4 x i16> poison, i16 %{{.*}}, i64 0
// CIR: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
// CIR: extractelement <4 x i32>
// CIR: call i32 @llvm.aarch64.neon.sqsub.i32(i32
// OGCG-LABEL: @test_vqdmlslh_laneq_s16
// OGCG: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
// OGCG: call i32 @llvm.aarch64.neon.sqsub.i32(i32
int32_t test_vqdmlslh_laneq_s16(int32_t a, int16_t b, int16x8_t c) {
  return vqdmlslh_laneq_s16(a, b, c, 7);
}

// CIR-LABEL: @test_vqdmlals_laneq_s32
// CIR: extractelement <4 x i32>
// CIR: call i64 @llvm.aarch64.neon.sqdmulls.scalar(i32
// CIR: call i64 @llvm.aarch64.neon.sqadd.i64(i64
// OGCG-LABEL: @test_vqdmlals_laneq_s32
// OGCG: call i64 @llvm.aarch64.neon.sqdmulls.scalar(i32
// OGCG: call i64 @llvm.aarch64.neon.sqadd.i64(i64
int64_t test_vqdmlals_laneq_s32(int64_t a, int32_t b, int32x4_t c) {
  return vqdmlals_laneq_s32(a, b, c, 3);
}

// CIR-LABEL: @test_vqdmlsls_lane_s32
// CIR: extractelement <2 x i32>
// CIR: call i64 @llvm.aarch64.neon.sqdmulls.scalar(i32
// CIR: call i64 @llvm.aarch64.neon.sqsub.i64(i64
// OGCG-LABEL: @test_vqdmlsls_lane_s32
// OGCG: call i64 @llvm.aarch64.neon.sqdmulls.scalar(i32
// OGCG: call i64 @llvm.aarch64.neon.sqsub.i64(i64
int64_t test_vqdmlsls_lane_s32(int64_t a, int32_t b, int32x2_t c) {
  return vqdmlsls_lane_s32(a, b, c, 1);
}
