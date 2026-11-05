// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*AnyFn)();
extern AnyFn any_fp_sig_reuse;

// Keep multiple identical no-proto mismatch call signatures in one function
// so slow-path arrangement can reuse prior signature metadata.
int slowpath_sig_reuse(int x, int y) {
  int a = any_fp_sig_reuse(x, y);
  int b = any_fp_sig_reuse(a, y);
  int c = any_fp_sig_reuse(b, y);
  return any_fp_sig_reuse(c, y);
}

// CHECK-LABEL: define{{.*}} i32 @slowpath_sig_reuse(
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
