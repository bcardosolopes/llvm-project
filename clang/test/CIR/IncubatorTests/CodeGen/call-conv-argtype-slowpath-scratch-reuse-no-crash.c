// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*AnyFn)();
extern AnyFn any_fp_a;
extern AnyFn any_fp_b;

// Exercise repeated slow-path call arrangement across multiple calls/functions.
int mismatch_calls_a(int x) {
  int a = any_fp_a(x);
  int b = any_fp_a(a, x);
  return any_fp_a(b, x, 1);
}

int mismatch_calls_b(int x) {
  int a = any_fp_b(x);
  int b = any_fp_b(a, x);
  return any_fp_b(b, x, 2);
}

// CHECK-LABEL: define{{.*}} i32 @mismatch_calls_a(
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}})
// CHECK-LABEL: define{{.*}} i32 @mismatch_calls_b(
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}})
