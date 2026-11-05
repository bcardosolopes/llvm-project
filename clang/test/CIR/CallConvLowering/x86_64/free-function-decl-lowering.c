// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

extern int ext_fn(int);
extern int ext_vararg(int, ...);

int caller(int x) {
  return ext_fn(x) + ext_vararg(x, 7);
}

// CHECK-LABEL: define dso_local i32 @caller
// CHECK: call i32 @ext_fn(i32 noundef
// CHECK: call i32 (i32, ...) @ext_vararg(i32 noundef
// CHECK-DAG: declare i32 @ext_fn(i32 noundef)
// CHECK-DAG: declare i32 @ext_vararg(i32 noundef, ...)
