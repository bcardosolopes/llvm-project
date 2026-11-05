// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +neon -fclangir -emit-llvm %s -o /dev/null

// NEON vcvt_f64_f32 — float32-to-float64 vector conversion (fixed)
#include <arm_neon.h>

float64x2_t test_vcvt_f64_f32(float32x2_t a) {
  return vcvt_f64_f32(a);
}
