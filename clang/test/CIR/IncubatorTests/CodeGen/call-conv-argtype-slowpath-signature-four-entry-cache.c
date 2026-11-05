// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*AnyFn)();
extern AnyFn any_fp_four_cache;

// Cycle among four mismatch signatures so the expanded slowpath signature cache
// can hold all shapes without immediate eviction.
int slowpath_sig_four_entry_cache(int a0, int a1, int a2, int a3) {
  int x0 = any_fp_four_cache(a0);
  int x1 = any_fp_four_cache(x0, a1);
  int x2 = any_fp_four_cache(x1, a1, a2);
  int x3 = any_fp_four_cache(x2, a1, a2, a3);
  int x4 = any_fp_four_cache(x3);
  int x5 = any_fp_four_cache(x4, a1);
  int x6 = any_fp_four_cache(x5, a1, a2);
  return any_fp_four_cache(x6, a1, a2, a3);
}

// CHECK-LABEL: define{{.*}} i32 @slowpath_sig_four_entry_cache(
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}}, i32{{.*}})
