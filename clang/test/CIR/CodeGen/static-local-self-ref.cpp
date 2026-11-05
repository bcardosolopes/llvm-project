// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++11 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++11 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Static local with self-referencing constexpr initializer should produce
// a single self-referencing global, not a spurious .1 duplicate.

struct S {
  constexpr S(int n, const S *p) : n(n), p(p) {}
  int n;
  const S *p;
  ~S();
};

S global_s(99, &global_s);

void test() {
  static S s(42, &s);
}

// CHECK-DAG: @global_s = global %struct.S { i32 99, ptr @global_s }, align 8
// CHECK-DAG: @_ZZ4testvE1s = internal global %struct.S { i32 42, ptr @_ZZ4testvE1s }, align 8
// CHECK-NOT: @_ZZ4testvE1s.1

// OGCG-DAG: @global_s = global %struct.S { i32 99, ptr @global_s }, align 8
// OGCG-DAG: @_ZZ4testvE1s = internal global %struct.S { i32 42, ptr @_ZZ4testvE1s }, align 8
// OGCG-NOT: @_ZZ4testvE1s.1
