// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int, int);
extern FnTy fp;

static int add(int a, int b) { return a + b; }

int repeated_indirect_calls(int x) {
  int s0 = fp(x, 1);
  int s1 = fp(s0, 2);
  int s2 = fp(s1, 3);
  int s3 = fp(s2, 4);
  int s4 = fp(s3, 5);
  int s5 = fp(s4, 6);
  int s6 = fp(s5, 7);
  int s7 = fp(s6, 8);
  return s7;
}

int repeated_direct_calls(int x) {
  int s0 = add(x, 1);
  int s1 = add(s0, 2);
  int s2 = add(s1, 3);
  int s3 = add(s2, 4);
  return s3;
}

// CHECK-LABEL: define{{.*}} i32 @repeated_indirect_calls(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK-LABEL: define{{.*}} i32 @repeated_direct_calls(
// CHECK: call i32 @add(i32
// CHECK: call i32 @add(i32
// CHECK: call i32 @add(i32
// CHECK: call i32 @add(i32
