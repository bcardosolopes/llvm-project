// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);
extern FnTy fp1;
extern FnTy fp2;
extern FnTy fp3;
extern FnTy fp4;
extern FnTy fp5;
extern FnTy fp6;
extern FnTy fp7;
extern FnTy fp8;
extern FnTy fp9;
extern FnTy fp10;
extern int non_fn_global;

static int add1(int v) { return v + 1; }

int call_chain(int x) {
  int y = fp1(x);
  int z = fp2(y);
  int w = fp3(z);
  int q = fp4(w);
  int r = fp5(q);
  int s = fp6(r);
  int t = fp7(s);
  int u = fp8(t);
  int v = fp9(u);
  return fp10(v);
}

int call_direct(int x) { return add1(x); }

int read_global_plus_call(int x) { return non_fn_global + fp1(x); }

// CHECK-LABEL: define{{.*}} i32 @call_chain(
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK: call i32 %{{.*}}(i32
// CHECK-LABEL: define{{.*}} i32 @call_direct(
// CHECK: call i32 @add1(i32
// CHECK-LABEL: define{{.*}} i32 @read_global_plus_call(
// CHECK: load i32, ptr @non_fn_global
// CHECK: call i32 %{{.*}}(i32
