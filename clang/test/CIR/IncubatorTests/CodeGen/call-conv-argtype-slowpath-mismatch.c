// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*AnyFn)();
extern AnyFn any_fp;

// This exercises the argument-type slow path in arrangeFreeFunctionLikeCall:
// call operands do not match callee prototype inputs, so lowering must build
// per-call arg type vectors and still produce correct indirect calls.
int mismatch_shape_calls(int x) {
  int a = any_fp(x);
  int b = any_fp(a, x);
  int c = any_fp(b, 1, 2);
  return any_fp(c, x, 3, 4);
}

// CHECK-LABEL: define{{.*}} i32 @mismatch_shape_calls(
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}}, i32{{.*}})
