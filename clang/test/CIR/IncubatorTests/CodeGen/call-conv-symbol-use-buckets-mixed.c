// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*FnTy)(int);

extern FnTy fp_bucket;
extern int decl_callee(int);

static int local_add1(int v) { return v + 1; }

int mixed_bucket_users(int x) {
  int y = local_add1(x);
  int z = decl_callee(y);
  return z + fp_bucket(z);
}

// CHECK-LABEL: define{{.*}} i32 @mixed_bucket_users(
// CHECK: call i32 @local_add1(i32
// CHECK: call i32 @decl_callee(i32
// CHECK: call i32 %{{.*}}(i32
