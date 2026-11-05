// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o - | FileCheck %s
// Regression: __builtin_omp_required_simd_align no longer crashes under CIR.

struct s0 { int x; };
int test(void) { return __builtin_omp_required_simd_align(struct s0); }

// CHECK: ret i32 16
