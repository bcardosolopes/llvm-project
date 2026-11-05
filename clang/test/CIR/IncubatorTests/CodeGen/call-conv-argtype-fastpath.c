// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int, int, int);
extern FnTy fp;

int match_proto_shape(int x) {
  int a = fp(x, 1, 2);
  int b = fp(a, 3, 4);
  int c = fp(b, 5, 6);
  return fp(c, 7, 8);
}

int variadic_like_mix(int x, int y) {
  int a = fp(x, y, 9);
  return fp(a, y, 10);
}

// CHECK-LABEL: define{{.*}} i32 @match_proto_shape(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK-LABEL: define{{.*}} i32 @variadic_like_mix(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
