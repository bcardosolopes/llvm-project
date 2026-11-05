// RUN: not %clang_cc1 -triple arm64-none-linux-gnu -target-feature +bf16 \
// RUN:   -fclangir -disable-O0-optnone -emit-cir -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s
// RUN: not %clang_cc1 -triple arm64-none-linux-gnu -target-feature +bf16 \
// RUN:   -fclangir -disable-O0-optnone -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s

#include <arm_neon.h>

bfloat16x4_t test_vdup_lane_bf16(bfloat16x4_t v) {
  return vdup_lane_bf16(v, 1);
}

// CHECK: error: '__builtin_neon_splat_lane_bf16' needs target feature bf16,neon
