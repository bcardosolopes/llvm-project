// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir -emit-cir \
// RUN:   -fcuda-is-device -x cuda %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test: [[clang::noconvergent]] removes convergent from calls in CUDA mode
// where all functions are convergent by default.

__attribute__((device)) void convergent_fn();

__attribute__((device)) void test() {
  convergent_fn();
  [[clang::noconvergent]] convergent_fn();
}

// The attr group for the noconvergent call should NOT have convergent.
// CHECK: #cir<extra({nothrow = #cir.nothrow})>
