// RUN: %clang_cc1 -triple aarch64-none-linux-android24  -fclangir \
// RUN:            -emit-cir -target-feature +neon %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android24  -fclangir \
// RUN:            -emit-llvm -target-feature +neon %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Testing normal situation of vdup lane intrinsics.

// REQUIRES: aarch64-registered-target || arm-registered-target
#include <arm_neon.h>

int8_t test_vdupb_lane_s8(int8x8_t src) {
  return vdupb_lane_s8(src, 7);
}

// CIR-LABEL: test_vdupb_lane_s8
// CIR: [[IDX:%.*]]  = cir.const #cir.int<7> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<8 x !u8i>

// LLVM: define dso_local i8 @test_vdupb_lane_s8(<8 x i8> noundef %{{.*}})
// LLVM: extractelement <8 x i8> %{{.*}}, i32 7
// LLVM: ret i8 %{{.*}}

int8_t test_vdupb_laneq_s8(int8x16_t a) {
  return vdupb_laneq_s8(a, 15);
}

// CIR-LABEL: test_vdupb_laneq_s8
// CIR: [[IDX:%.*]]  = cir.const #cir.int<15> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<16 x !u8i>

// LLVM: define dso_local i8 @test_vdupb_laneq_s8(<16 x i8> noundef %{{.*}})
// LLVM: extractelement <16 x i8> %{{.*}}, i32 15
// LLVM: ret i8 %{{.*}}

int16_t test_vduph_lane_s16(int16x4_t src) {
  return vduph_lane_s16(src, 3);
}

// CIR-LABEL: test_vduph_lane_s16
// CIR: [[IDX:%.*]]  = cir.const #cir.int<3> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<4 x !u16i>

// LLVM: define dso_local i16 @test_vduph_lane_s16(<4 x i16> noundef %{{.*}})
// LLVM: extractelement <4 x i16> %{{.*}}, i32 3
// LLVM: ret i16 %{{.*}}

int16_t test_vduph_laneq_s16(int16x8_t a) {
  return vduph_laneq_s16(a, 7);
}

// CIR-LABEL: test_vduph_laneq_s16
// CIR: [[IDX:%.*]]  = cir.const #cir.int<7> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<8 x !u16i>

// LLVM: define dso_local i16 @test_vduph_laneq_s16(<8 x i16> noundef %{{.*}})
// LLVM: extractelement <8 x i16> %{{.*}}, i32 7
// LLVM: ret i16 %{{.*}}

int32_t test_vdups_lane_s32(int32x2_t a) {
  return vdups_lane_s32(a, 1);
}

// CIR-LABEL: test_vdups_lane_s32
// CIR: [[IDX:%.*]]  = cir.const #cir.int<1> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<2 x !u32i>

// LLVM: define dso_local i32 @test_vdups_lane_s32(<2 x i32> noundef %{{.*}})
// LLVM: extractelement <2 x i32> %{{.*}}, i32 1
// LLVM: ret i32 %{{.*}}

int32_t test_vdups_laneq_s32(int32x4_t a) {
  return vdups_laneq_s32(a, 3);
}

// CIR-LABEL: test_vdups_laneq_s32
// CIR: [[IDX:%.*]]  = cir.const #cir.int<3> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<4 x !u32i>

// LLVM: define dso_local i32 @test_vdups_laneq_s32(<4 x i32> noundef %{{.*}})
// LLVM: extractelement <4 x i32> %{{.*}}, i32 3
// LLVM: ret i32 %{{.*}}

int64_t test_vdupd_lane_s64(int64x1_t src) {
  return vdupd_lane_s64(src, 0);
}

// CIR-LABEL: test_vdupd_lane_s64
// CIR: [[IDX:%.*]]  = cir.const #cir.int<0> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<1 x !u64i>

// LLVM: define dso_local i64 @test_vdupd_lane_s64(<1 x i64> noundef %{{.*}})
// LLVM: extractelement <1 x i64> %{{.*}}, i32 0
// LLVM: ret i64 %{{.*}}

int64_t test_vdupd_laneq_s64(int64x2_t a) {
  return vdupd_laneq_s64(a, 1);
}

// CIR-LABEL: test_vdupd_laneq_s64
// CIR: [[IDX:%.*]]  = cir.const #cir.int<1> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<2 x !u64i>

// LLVM: define dso_local i64 @test_vdupd_laneq_s64(<2 x i64> noundef %{{.*}})
// LLVM: extractelement <2 x i64> %{{.*}}, i32 1
// LLVM: ret i64 %{{.*}}

float32_t test_vdups_lane_f32(float32x2_t src) {
  return vdups_lane_f32(src, 1);
}

// CIR-LABEL: test_vdups_lane_f32
// CIR: [[IDX:%.*]]  = cir.const #cir.int<1> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<2 x !cir.float>

// LLVM: define dso_local float @test_vdups_lane_f32(<2 x float> noundef %{{.*}})
// LLVM: extractelement <2 x float> %{{.*}}, i32 1
// LLVM: ret float %{{.*}}

float64_t test_vdupd_lane_f64(float64x1_t src) {
  return vdupd_lane_f64(src, 0);
}

// CIR-LABEL: test_vdupd_lane_f64
// CIR: [[IDX:%.*]]  = cir.const #cir.int<0> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<1 x !cir.double>

// LLVM: define dso_local double @test_vdupd_lane_f64(<1 x double> noundef %{{.*}})
// LLVM: extractelement <1 x double> %{{.*}}, i32 0
// LLVM: ret double %{{.*}}

float32_t test_vdups_laneq_f32(float32x4_t src) {
  return vdups_laneq_f32(src, 3);
}

// CIR-LABEL: test_vdups_laneq_f32
// CIR: [[IDX:%.*]]  = cir.const #cir.int<3> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<4 x !cir.float>

// LLVM: define dso_local float @test_vdups_laneq_f32(<4 x float> noundef %{{.*}})
// LLVM: extractelement <4 x float> %{{.*}}, i32 3
// LLVM: ret float %{{.*}}

float64_t test_vdupd_laneq_f64(float64x2_t src) {
  return vdupd_laneq_f64(src, 1);
}

// CIR-LABEL: test_vdupd_laneq_f64
// CIR: [[IDX:%.*]]  = cir.const #cir.int<1> : !s32i
// CIR: {{%.*}} = cir.vec.extract {{%.*}}[[[IDX]] : !s32i] : !cir.vector<2 x !cir.double>

// LLVM: define dso_local double @test_vdupd_laneq_f64(<2 x double> noundef %{{.*}})
// LLVM: extractelement <2 x double> %{{.*}}, i32 1
// LLVM: ret double %{{.*}}
