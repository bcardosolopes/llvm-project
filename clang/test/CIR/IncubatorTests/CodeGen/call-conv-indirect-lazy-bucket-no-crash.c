// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);

int apply_twice(FnTy fn, int x) {
  int y = fn(x);
  return fn(y);
}

// CHECK-LABEL: define{{.*}} i32 @apply_twice(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
