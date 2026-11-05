// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);

extern int ext_target(int);
extern FnTy fp_ext_target;

int many_symbol_users(int x) {
  int a = ext_target(x);
  int b = ext_target(a);
  int c = ext_target(b);
  int d = ext_target(c);
  return fp_ext_target(d);
}

// CHECK-LABEL: define{{.*}} i32 @many_symbol_users(
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 %{{.*}}(i32
