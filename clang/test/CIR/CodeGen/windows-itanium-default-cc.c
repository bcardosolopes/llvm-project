// RUN: %clang_cc1 -triple thumbv7--windows-itanium -emit-llvm -fclangir -o - %s | FileCheck %s
// REQUIRES: arm-registered-target

__attribute__((noinline)) void f(void) {}

void g(void) {
  f();
}

// CHECK-LABEL: define dso_local arm_aapcs_vfpcc void @f()
// CHECK-LABEL: define dso_local arm_aapcs_vfpcc void @g()
// CHECK: call arm_aapcs_vfpcc void @f()
