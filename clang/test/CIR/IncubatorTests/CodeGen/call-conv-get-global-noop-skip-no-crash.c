// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);
extern int helper(int);

FnTy get_helper_ptr(void) { return helper; }

int call_via_local_fp(int x) {
  FnTy fn = helper;
  return fn(x);
}

// CHECK-LABEL: define{{.*}} ptr @get_helper_ptr(
// CHECK: ret ptr @helper
// CHECK-LABEL: define{{.*}} i32 @call_via_local_fp(
// CHECK: store ptr @helper
// CHECK: call i32 %{{.*}}(i32
