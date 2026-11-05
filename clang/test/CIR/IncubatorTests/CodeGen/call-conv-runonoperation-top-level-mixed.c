// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

// Regression coverage for CallConvLowering pass runOnOperation traversal:
// keep top-level non-function ops (globals) interleaved with functions and
// ensure function lowering remains stable.

extern int ext_mixed(int);

int g_mixed = 7;

int f_mixed_a(int x) { return ext_mixed(x) + g_mixed; }
int f_mixed_b(int x) { return ext_mixed(x + 1); }

// CHECK-LABEL: @g_mixed =
// CHECK-LABEL: define{{.*}} i32 @f_mixed_a(
// CHECK: call i32 @ext_mixed(i32
// CHECK-LABEL: define{{.*}} i32 @f_mixed_b(
// CHECK: call i32 @ext_mixed(i32
