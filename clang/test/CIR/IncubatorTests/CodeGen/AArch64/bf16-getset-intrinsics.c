// RUN: %clang_cc1 -triple arm64-none-linux-gnu -target-feature +neon -target-feature +bf16 \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple arm64-none-linux-gnu -target-feature +neon -target-feature +bf16 \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-llvm -fno-clangir-call-conv-lowering -o - %s \
// RUN: | opt -S -passes=mem2reg,sroa,instcombine,simplifycfg -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// REQUIRES: aarch64-registered-target || arm-registered-target

// This test mimics clang/test/CodeGen/AArch64/bf16-getset-intrinsics.c, which eventually
// CIR shall be able to support fully. Since this is going to take some time to converge,
// the unsupported/NYI code is commented out, so that we can incrementally improve this.
// The NYI filecheck used contains the LLVM output from OG codegen that should guide the
// correct result when implementing this into the CIR pipeline.

#include <arm_neon.h>

// LLVM-LABEL: test_vcreate_bf16
// LLVM: [[BITCAST:%.*]] = bitcast i64 {{%.*}} to <4 x bfloat>
// LLVM: ret <4 x bfloat> [[BITCAST]]
bfloat16x4_t test_vcreate_bf16(uint64_t a) {
  return vcreate_bf16(a);
}

// LLVM-LABEL: test_vdup_n_bf16
// LLVM: [[INSERT:%.*]] = insertelement <4 x bfloat> poison, bfloat [[V:%.*]], i64 0
// LLVM: [[SHUFFLE:%.*]] = shufflevector <4 x bfloat> [[INSERT]], <4 x bfloat> poison, <4 x i32> zeroinitializer
// LLVM: ret <4 x bfloat> [[SHUFFLE]]
bfloat16x4_t test_vdup_n_bf16(bfloat16_t v) {
  return vdup_n_bf16(v);
}

// LLVM-LABEL: test_vdupq_n_bf16
// LLVM: [[INSERT:%.*]] = insertelement <8 x bfloat> poison, bfloat [[V:%.*]], i64 0
// LLVM: [[SHUFFLE:%.*]] = shufflevector <8 x bfloat> [[INSERT]], <8 x bfloat> poison, <8 x i32> zeroinitializer
// LLVM: ret <8 x bfloat> [[SHUFFLE]]
bfloat16x8_t test_vdupq_n_bf16(bfloat16_t v) {
  return vdupq_n_bf16(v);
}

// LLVM-LABEL: test_vdup_lane_bf16
// LLVM-SAME: (<4 x bfloat>{{( noundef)?}} [[V:%.*]])
// LLVM: [[LANE:%.*]] = shufflevector <4 x bfloat> [[V]], <4 x bfloat> poison, <4 x i32> <i32 1, i32 1, i32 1, i32 1>
// LLVM: ret <4 x bfloat> [[LANE]]
bfloat16x4_t test_vdup_lane_bf16(bfloat16x4_t v) {
  return vdup_lane_bf16(v, 1);
}

// LLVM-LABEL: test_vdupq_lane_bf16
// LLVM-SAME: (<4 x bfloat>{{( noundef)?}} [[V:%.*]])
// LLVM: [[LANE:%.*]] = shufflevector <4 x bfloat> [[V]], <4 x bfloat> poison, <8 x i32> <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
// LLVM: ret <8 x bfloat> [[LANE]]
bfloat16x8_t test_vdupq_lane_bf16(bfloat16x4_t v) {
  return vdupq_lane_bf16(v, 1);
}

// LLVM-LABEL: test_vdup_laneq_bf16
// LLVM-SAME: (<8 x bfloat>{{( noundef)?}} [[V:%.*]])
// LLVM: [[LANE:%.*]] = shufflevector <8 x bfloat> [[V]], <8 x bfloat> poison, <4 x i32> <i32 7, i32 7, i32 7, i32 7>
// LLVM: ret <4 x bfloat> [[LANE]]
bfloat16x4_t test_vdup_laneq_bf16(bfloat16x8_t v) {
  return vdup_laneq_bf16(v, 7);
}

// LLVM-LABEL: test_vdupq_laneq_bf16
// LLVM-SAME: (<8 x bfloat>{{( noundef)?}} [[V:%.*]])
// LLVM: [[LANE:%.*]] = shufflevector <8 x bfloat> [[V]], <8 x bfloat> poison, <8 x i32> <i32 7, i32 7, i32 7, i32 7, i32 7, i32 7, i32 7, i32 7>
// LLVM: ret <8 x bfloat> [[LANE]]
bfloat16x8_t test_vdupq_laneq_bf16(bfloat16x8_t v) {
  return vdupq_laneq_bf16(v, 7);
}

// LLVM-LABEL: test_vcombine_bf16
// LLVM-SAME: (<4 x bfloat>{{( noundef)?}} [[LOW:%.*]], <4 x bfloat>{{( noundef)?}} [[HIGH:%.*]])
// LLVM: [[SHUFFLE:%.*]] = shufflevector <4 x bfloat> [[LOW]], <4 x bfloat> [[HIGH]], <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
// LLVM: ret <8 x bfloat> [[SHUFFLE]]
bfloat16x8_t test_vcombine_bf16(bfloat16x4_t low, bfloat16x4_t high) {
  return vcombine_bf16(low, high);
}

// LLVM-LABEL: test_vget_high_bf16
// LLVM-SAME: (<8 x bfloat>{{( noundef)?}} [[A:%.*]])
// LLVM: [[SHUFFLE:%.*]] = shufflevector <8 x bfloat> [[A]], <8 x bfloat> poison, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
// LLVM: ret <4 x bfloat> [[SHUFFLE]]
bfloat16x4_t test_vget_high_bf16(bfloat16x8_t a) {
  return vget_high_bf16(a);
}

// LLVM-LABEL: test_vget_low_bf16
// LLVM-SAME: (<8 x bfloat>{{( noundef)?}} [[A:%.*]])
// LLVM: [[SHUFFLE:%.*]] = shufflevector <8 x bfloat> [[A]], <8 x bfloat> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
// LLVM: ret <4 x bfloat> [[SHUFFLE]]
bfloat16x4_t test_vget_low_bf16(bfloat16x8_t a) {
  return vget_low_bf16(a);
}

bfloat16_t test_vget_lane_bf16(bfloat16x4_t v) {
 return vget_lane_bf16(v, 1);

  // CIR-LABEL: vget_lane_bf16
  // CIR: [[TMP0:%.*]] = cir.const #cir.int<1> : !s32i
  // CIR: [[TMP1:%.*]] = cir.vec.extract {{.*}}[{{.*}} : !s32i] : !cir.vector<4 x !cir.bf16>

  // LLVM-LABEL: test_vget_lane_bf16
  // LLVM-SAME: (<4 x bfloat>{{( noundef)?}} [[VEC:%.*]])
  // LLVM: [[VGET_LANE:%.*]] = extractelement <4 x bfloat> [[VEC]], i64 1
  // LLVM: ret bfloat [[VGET_LANE]]
}

bfloat16_t test_vgetq_lane_bf16(bfloat16x8_t v) {
  return vgetq_lane_bf16(v, 7);

  // CIR-LABEL: vgetq_lane_bf16
  // CIR: [[TMP0:%.*]] = cir.const #cir.int<7> : !s32i
  // CIR: [[TMP1:%.*]] = cir.vec.extract {{.*}}[{{.*}} : !s32i] : !cir.vector<8 x !cir.bf16>

  // LLVM-LABEL: test_vgetq_lane_bf16
  // LLVM-SAME: (<8 x bfloat>{{( noundef)?}} [[VEC:%.*]])
  // LLVM: [[VGET_LANE:%.*]] = extractelement <8 x bfloat> [[VEC]], i64 7
  // LLVM: ret bfloat [[VGET_LANE]]
}

// LLVM-LABEL: test_vset_lane_bf16
// LLVM-SAME: (bfloat{{( noundef)?}} [[A:%.*]], <4 x bfloat>{{( noundef)?}} [[V:%.*]])
// LLVM: [[INSERT:%.*]] = insertelement <4 x bfloat> [[V]], bfloat [[A]], i64 1
// LLVM: ret <4 x bfloat> [[INSERT]]
bfloat16x4_t test_vset_lane_bf16(bfloat16_t a, bfloat16x4_t v) {
  return vset_lane_bf16(a, v, 1);
}

// LLVM-LABEL: test_vsetq_lane_bf16
// LLVM-SAME: (bfloat{{( noundef)?}} [[A:%.*]], <8 x bfloat>{{( noundef)?}} [[V:%.*]])
// LLVM: [[INSERT:%.*]] = insertelement <8 x bfloat> [[V]], bfloat [[A]], i64 7
// LLVM: ret <8 x bfloat> [[INSERT]]
bfloat16x8_t test_vsetq_lane_bf16(bfloat16_t a, bfloat16x8_t v) {
 return vsetq_lane_bf16(a, v, 7);
}

bfloat16_t test_vduph_lane_bf16(bfloat16x4_t v) {
 return vduph_lane_bf16(v, 1);

  // CIR-LABEL: vduph_lane_bf16
  // CIR: [[TMP0:%.*]] = cir.const #cir.int<1> : !s32i
  // CIR: [[TMP1:%.*]] = cir.vec.extract {{.*}}[{{.*}} : !s32i] : !cir.vector<4 x !cir.bf16>

  // LLVM-LABEL: test_vduph_lane_bf16
  // LLVM-SAME: (<4 x bfloat>{{( noundef)?}} [[VEC:%.*]])
  // LLVM: [[VGET_LANE:%.*]] = extractelement <4 x bfloat> [[VEC]], i64 1
  // LLVM: ret bfloat [[VGET_LANE]]
}
