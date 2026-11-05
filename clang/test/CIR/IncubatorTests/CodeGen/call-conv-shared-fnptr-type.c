// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);

extern FnTy fp_a;
extern FnTy fp_b;
extern FnTy fp_c;

int call_many(int x) {
  int y = fp_a(x);
  int z = fp_b(y);
  return fp_c(z);
}

// CHECK-LABEL: define{{.*}} i32 @call_many(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
