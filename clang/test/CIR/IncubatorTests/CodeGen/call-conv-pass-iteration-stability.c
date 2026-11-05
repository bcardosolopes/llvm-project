// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);
extern FnTy fp_iter;

static int plus1(int v) { return v + 1; }
static int plus2(int v) { return v + 2; }

int iter_entry(int x) { return plus1(x); }
int iter_mid(int x) { return fp_iter(x); }
int iter_tail(int x) { return plus2(fp_iter(x)); }

// CHECK-LABEL: define{{.*}} i32 @iter_entry(
// CHECK: call i32 @plus1(i32
// CHECK-LABEL: define{{.*}} i32 @iter_mid(
// CHECK: call i32 %{{.*}}(i32
// CHECK-LABEL: define{{.*}} i32 @iter_tail(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 @plus2(i32
