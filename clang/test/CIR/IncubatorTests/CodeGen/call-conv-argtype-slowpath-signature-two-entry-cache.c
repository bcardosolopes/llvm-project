// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

typedef int (*AnyFn)();
extern AnyFn any_fp_two_cache;

// Alternate two distinct mismatch signatures so the two-entry slowpath
// signature cache gets exercised (A, B, A, B).
int slowpath_sig_two_entry_cache(int x, int y) {
  int a = any_fp_two_cache(x, y);
  int b = any_fp_two_cache(a);
  int c = any_fp_two_cache(b, y);
  int d = any_fp_two_cache(c);
  return d;
}

// Revisit an older shape after introducing a third one to exercise
// non-adjacent cache hits (preferred-slot probing).
int slowpath_sig_two_entry_cache_revisit(int x, int y, int z) {
  int a = any_fp_two_cache(x);
  int b = any_fp_two_cache(a, y);
  int c = any_fp_two_cache(b, y, z);
  int d = any_fp_two_cache(c, y);
  return any_fp_two_cache(d);
}

// CHECK-LABEL: define{{.*}} i32 @slowpath_sig_two_entry_cache(
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})

// CHECK-LABEL: define{{.*}} i32 @slowpath_sig_two_entry_cache_revisit(
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, i32, ...) %{{.*}}(i32{{.*}}, i32{{.*}})
// CHECK: call i32 (i32, ...) %{{.*}}(i32{{.*}})
