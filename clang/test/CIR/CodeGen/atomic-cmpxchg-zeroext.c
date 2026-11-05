// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: __atomic_compare_exchange call sites must have
// zeroext on the i1 return type and noundef on all arguments, matching OGCG.

typedef struct { int x[20]; } Large;

_Bool test_atomic_cmpxchg(Large *ptr, Large *expected, Large *desired) {
  return __atomic_compare_exchange(ptr, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// The declaration must have zeroext on the i1 return and noundef on all params.
// CHECK-DAG: declare zeroext i1 @__atomic_compare_exchange(i64 noundef, ptr noundef, ptr noundef, ptr noundef, i32 noundef, i32 noundef)
// The call site must propagate zeroext and noundef.
// CHECK-DAG: call zeroext i1 @__atomic_compare_exchange(i64 noundef 80, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i32 noundef 5, i32 noundef 5)
