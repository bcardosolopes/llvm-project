// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon -target-feature +lut -disable-O0-optnone -fclangir -emit-llvm -o - %s | opt -S -p mem2reg,sroa | FileCheck %s
// REQUIRES: aarch64-registered-target

#include <arm_neon.h>

// CHECK-LABEL: define dso_local <8 x i8> @test_vset_lane_mf8(
// CHECK-SAME: <1 x i8> [[A:%.*]], <8 x i8> [[B:%.*]])
// CHECK: [[TMP0:%.*]] = extractelement <1 x i8> [[A]], i32 0
// CHECK: [[VSET:%.*]] = insertelement <8 x i8> [[B]], i8 [[TMP0]], i32 7
// CHECK: ret <8 x i8> [[VSET]]
mfloat8x8_t test_vset_lane_mf8(mfloat8_t a, mfloat8x8_t b) {
  return vset_lane_mf8(a, b, 7);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vsetq_lane_mf8(
// CHECK-SAME: <1 x i8> [[A:%.*]], <16 x i8> [[B:%.*]])
// CHECK: [[TMP0:%.*]] = extractelement <1 x i8> [[A]], i32 0
// CHECK: [[VSET:%.*]] = insertelement <16 x i8> [[B]], i8 [[TMP0]], i32 15
// CHECK: ret <16 x i8> [[VSET]]
mfloat8x16_t test_vsetq_lane_mf8(mfloat8_t a, mfloat8x16_t b) {
  return vsetq_lane_mf8(a, b, 15);
}

// CHECK-LABEL: define dso_local <1 x i8> @test_vget_lane_mf8(
// CHECK-SAME: <8 x i8> [[A:%.*]])
// CHECK: [[VGET:%.*]] = extractelement <8 x i8> [[A]], i32 7
// CHECK: [[TMP0:%.*]] = bitcast i8 [[VGET]] to <1 x i8>
// CHECK: ret <1 x i8> [[TMP0]]
mfloat8_t test_vget_lane_mf8(mfloat8x8_t a) {
  return vget_lane_mf8(a, 7);
}

// CHECK-LABEL: define dso_local <1 x i8> @test_vdupb_lane_mf8(
// CHECK-SAME: <8 x i8> [[A:%.*]])
// CHECK: [[VGET:%.*]] = extractelement <8 x i8> [[A]], i32 7
// CHECK: [[TMP0:%.*]] = bitcast i8 [[VGET]] to <1 x i8>
// CHECK: ret <1 x i8> [[TMP0]]
mfloat8_t test_vdupb_lane_mf8(mfloat8x8_t a) {
  return vdupb_lane_mf8(a, 7);
}

// CHECK-LABEL: define dso_local <1 x i8> @test_vgetq_lane_mf8(
// CHECK-SAME: <16 x i8> [[A:%.*]])
// CHECK: [[VGET:%.*]] = extractelement <16 x i8> [[A]], i32 15
// CHECK: [[TMP0:%.*]] = bitcast i8 [[VGET]] to <1 x i8>
// CHECK: ret <1 x i8> [[TMP0]]
mfloat8_t test_vgetq_lane_mf8(mfloat8x16_t a) {
  return vgetq_lane_mf8(a, 15);
}

// CHECK-LABEL: define dso_local <1 x i8> @test_vdupb_laneq_mf8(
// CHECK-SAME: <16 x i8> [[A:%.*]])
// CHECK: [[VGET:%.*]] = extractelement <16 x i8> [[A]], i32 15
// CHECK: [[TMP0:%.*]] = bitcast i8 [[VGET]] to <1 x i8>
// CHECK: ret <1 x i8> [[TMP0]]
mfloat8_t test_vdupb_laneq_mf8(mfloat8x16_t a) {
  return vdupb_laneq_mf8(a, 15);
}

// CHECK-LABEL: define dso_local <8 x i8> @test_vdup_n_mf8(
// CHECK-SAME: <1 x i8> [[A:%.*]])
// CHECK: extractelement <1 x i8> [[A]], i32 0
mfloat8x8_t test_vdup_n_mf8(mfloat8_t a) {
  return vdup_n_mf8(a);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vdupq_n_mf8(
// CHECK-SAME: <1 x i8> [[A:%.*]])
// CHECK: extractelement <1 x i8> [[A]], i32 0
mfloat8x16_t test_vdupq_n_mf8(mfloat8_t a) {
  return vdupq_n_mf8(a);
}

// CHECK-LABEL: define dso_local <8 x i8> @test_vmov_n_mf8(
// CHECK-SAME: <1 x i8> [[A:%.*]])
// CHECK: extractelement <1 x i8> [[A]], i32 0
mfloat8x8_t test_vmov_n_mf8(mfloat8_t a) {
  return vmov_n_mf8(a);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vmovq_n_mf8(
// CHECK-SAME: <1 x i8> [[A:%.*]])
// CHECK: extractelement <1 x i8> [[A]], i32 0
mfloat8x16_t test_vmovq_n_mf8(mfloat8_t a) {
  return vmovq_n_mf8(a);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vluti2_lane_mf8(
// CHECK-SAME: <8 x i8> [[VN:%.*]], <8 x i8> noundef [[VM:%.*]])
// CHECK: [[VLUTI2:%.*]] = call <16 x i8> @llvm.aarch64.neon.vluti2.lane.v16i8.v8i8(<8 x i8> [[VN]], <8 x i8> [[VM]], i32 0)
// CHECK: ret <16 x i8> [[VLUTI2]]
mfloat8x16_t test_vluti2_lane_mf8(mfloat8x8_t vn, uint8x8_t vm) {
  return vluti2_lane_mf8(vn, vm, 0);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vluti2q_lane_mf8(
// CHECK-SAME: <16 x i8> [[VN:%.*]], <8 x i8> noundef [[VM:%.*]])
// CHECK: [[VLUTI2:%.*]] = call <16 x i8> @llvm.aarch64.neon.vluti2.lane.v16i8.v16i8(<16 x i8> [[VN]], <8 x i8> [[VM]], i32 1)
// CHECK: ret <16 x i8> [[VLUTI2]]
mfloat8x16_t test_vluti2q_lane_mf8(mfloat8x16_t vn, uint8x8_t vm) {
  return vluti2q_lane_mf8(vn, vm, 1);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vluti2_laneq_mf8(
// CHECK-SAME: <8 x i8> [[VN:%.*]], <16 x i8> noundef [[VM:%.*]])
// CHECK: [[VLUTI2:%.*]] = call <16 x i8> @llvm.aarch64.neon.vluti2.laneq.v16i8.v8i8(<8 x i8> [[VN]], <16 x i8> [[VM]], i32 0)
// CHECK: ret <16 x i8> [[VLUTI2]]
mfloat8x16_t test_vluti2_laneq_mf8(mfloat8x8_t vn, uint8x16_t vm) {
  return vluti2_laneq_mf8(vn, vm, 0);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vluti2q_laneq_mf8(
// CHECK-SAME: <16 x i8> [[VN:%.*]], <16 x i8> noundef [[VM:%.*]])
// CHECK: [[VLUTI2:%.*]] = call <16 x i8> @llvm.aarch64.neon.vluti2.laneq.v16i8.v16i8(<16 x i8> [[VN]], <16 x i8> [[VM]], i32 3)
// CHECK: ret <16 x i8> [[VLUTI2]]
mfloat8x16_t test_vluti2q_laneq_mf8(mfloat8x16_t vn, uint8x16_t vm) {
  return vluti2q_laneq_mf8(vn, vm, 3);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vluti4q_lane_mf8(
// CHECK-SAME: <16 x i8> [[VN:%.*]], <8 x i8> noundef [[VM:%.*]])
// CHECK: [[VLUTI4:%.*]] = call <16 x i8> @llvm.aarch64.neon.vluti4q.lane.v16i8(<16 x i8> [[VN]], <8 x i8> [[VM]], i32 0)
// CHECK: ret <16 x i8> [[VLUTI4]]
mfloat8x16_t test_vluti4q_lane_mf8(mfloat8x16_t vn, uint8x8_t vm) {
  return vluti4q_lane_mf8(vn, vm, 0);
}

// CHECK-LABEL: define dso_local <16 x i8> @test_vluti4q_laneq_mf8(
// CHECK-SAME: <16 x i8> [[VN:%.*]], <16 x i8> noundef [[VM:%.*]])
// CHECK: [[VLUTI4:%.*]] = call <16 x i8> @llvm.aarch64.neon.vluti4q.laneq.v16i8(<16 x i8> [[VN]], <16 x i8> [[VM]], i32 1)
// CHECK: ret <16 x i8> [[VLUTI4]]
mfloat8x16_t test_vluti4q_laneq_mf8(mfloat8x16_t vn, uint8x16_t vm) {
  return vluti4q_laneq_mf8(vn, vm, 1);
}
