// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);
extern FnTy fp_plan;
extern int non_fn_global_plan;

static int add1(int v) { return v + 1; }

int plan_direct(int x) { return add1(x); }
int plan_indirect(int x) { return fp_plan(x); }
int plan_global_plus_indirect(int x) { return non_fn_global_plan + fp_plan(x); }

// CHECK-LABEL: define{{.*}} i32 @plan_direct(
// CHECK: call i32 @add1(i32
// CHECK-LABEL: define{{.*}} i32 @plan_indirect(
// CHECK: call i32 %{{.*}}(i32
// CHECK-LABEL: define{{.*}} i32 @plan_global_plus_indirect(
// CHECK: load i32, ptr @non_fn_global_plan
// CHECK: call i32 %{{.*}}(i32
