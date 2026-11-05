// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vld4_lane_s32
// CIR: call { <2 x i32>, <2 x i32>, <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld4lane.v2i32.p0
// OGCG-LABEL: @test_vld4_lane_s32
// OGCG: call { <2 x i32>, <2 x i32>, <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld4lane.v2i32.p0
int32x2x4_t test_vld4_lane_s32(const int32_t *ptr, int32x2x4_t src) {
  return vld4_lane_s32(ptr, src, 1);
}

// CIR-LABEL: @test_vst2_lane_s32
// CIR: call void @llvm.aarch64.neon.st2lane.v2i32.p0(<2 x i32>
// OGCG-LABEL: @test_vst2_lane_s32
// OGCG: call void @llvm.aarch64.neon.st2lane.v2i32.p0(<2 x i32>
void test_vst2_lane_s32(int32_t *ptr, int32x2x2_t src) {
  vst2_lane_s32(ptr, src, 1);
}

// CIR-LABEL: @test_vst3_lane_s32
// CIR: call void @llvm.aarch64.neon.st3lane.v2i32.p0(<2 x i32>
// OGCG-LABEL: @test_vst3_lane_s32
// OGCG: call void @llvm.aarch64.neon.st3lane.v2i32.p0(<2 x i32>
void test_vst3_lane_s32(int32_t *ptr, int32x2x3_t src) {
  vst3_lane_s32(ptr, src, 1);
}

// CIR-LABEL: @test_vst4_lane_s32
// CIR: call void @llvm.aarch64.neon.st4lane.v2i32.p0(<2 x i32>
// OGCG-LABEL: @test_vst4_lane_s32
// OGCG: call void @llvm.aarch64.neon.st4lane.v2i32.p0(<2 x i32>
void test_vst4_lane_s32(int32_t *ptr, int32x2x4_t src) {
  vst4_lane_s32(ptr, src, 1);
}
