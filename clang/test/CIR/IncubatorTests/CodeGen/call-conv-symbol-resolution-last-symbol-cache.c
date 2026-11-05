// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

// Compile-time regression:
// stress repeated, consecutive identical symbol resolution sites so the
// resolveCallee last-symbol fast cache is exercised while preserving behavior.

typedef int (*FnTy)(int);

extern int ext_target(int);
extern FnTy fp_ext_target;

int many_symbol_users_last_symbol_cache(int x) {
  int a = ext_target(x);
  int b = ext_target(a);
  int c = ext_target(b);
  int d = ext_target(c);
  int e = ext_target(d);
  int f = ext_target(e);
  int g = ext_target(f);
  int h = ext_target(g);
  return fp_ext_target(h);
}

// CHECK-LABEL: define{{.*}} i32 @many_symbol_users_last_symbol_cache(
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 @ext_target(i32
// CHECK: call i32 %{{.*}}(i32

