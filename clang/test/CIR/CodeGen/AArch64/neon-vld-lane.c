// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-llvm -fno-clangir-call-conv-lowering -o - %s \
// RUN: | opt -S -passes=mem2reg,simplifycfg -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// REQUIRES: aarch64-registered-target || arm-registered-target

// Test vld{2,3,4}_lane and vld{2,3,4}q_lane NEON intrinsics.
// These load N elements from memory into the specified lane of N vectors.

#include <arm_neon.h>

// vld2_lane
int32x2x2_t test_vld2_lane_s32(int32_t const *ptr, int32x2x2_t src) {
    return vld2_lane_s32(ptr, src, 1);
}

// CIR-LABEL: test_vld2_lane_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld2lane"

// LLVM-LABEL: @test_vld2_lane_s32
// LLVM: call { <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld2lane.v2i32.p0

// vld2q_lane
int32x4x2_t test_vld2q_lane_s32(int32_t const *ptr, int32x4x2_t src) {
    return vld2q_lane_s32(ptr, src, 3);
}

// CIR-LABEL: test_vld2q_lane_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld2lane"

// LLVM-LABEL: @test_vld2q_lane_s32
// LLVM: call { <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld2lane.v4i32.p0

// vld3_lane
int32x2x3_t test_vld3_lane_s32(int32_t const *ptr, int32x2x3_t src) {
    return vld3_lane_s32(ptr, src, 1);
}

// CIR-LABEL: test_vld3_lane_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld3lane"

// LLVM-LABEL: @test_vld3_lane_s32
// LLVM: call { <2 x i32>, <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld3lane.v2i32.p0

// vld3q_lane
int32x4x3_t test_vld3q_lane_s32(int32_t const *ptr, int32x4x3_t src) {
    return vld3q_lane_s32(ptr, src, 3);
}

// CIR-LABEL: test_vld3q_lane_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld3lane"

// LLVM-LABEL: @test_vld3q_lane_s32
// LLVM: call { <4 x i32>, <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld3lane.v4i32.p0
