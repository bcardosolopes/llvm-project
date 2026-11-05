// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vmulxh_lane_f16
// CIR: call half @llvm.aarch64.neon.fmulx.f16(half
// OGCG-LABEL: @test_vmulxh_lane_f16
// OGCG: call half @llvm.aarch64.neon.fmulx.f16(half
float16_t test_vmulxh_lane_f16(float16_t a, float16x4_t b) {
  return vmulxh_lane_f16(a, b, 0);
}

// CIR-LABEL: @test_vmulxh_laneq_f16
// CIR: call half @llvm.aarch64.neon.fmulx.f16(half
// OGCG-LABEL: @test_vmulxh_laneq_f16
// OGCG: call half @llvm.aarch64.neon.fmulx.f16(half
float16_t test_vmulxh_laneq_f16(float16_t a, float16x8_t b) {
  return vmulxh_laneq_f16(a, b, 0);
}
