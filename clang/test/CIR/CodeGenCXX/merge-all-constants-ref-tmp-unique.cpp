// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fmerge-all-constants -fclangir -emit-cir %s -o - | FileCheck %s

struct E {};

int sink(const E &e = {}) {
  (void)e;
  return 1;
}

int f() { return sink(); }
int g() { return sink(); }

// Ensure merge-all-constants reference-temporary globals are uniqued at module
// scope even when they would otherwise share the same base name.
// CHECK-DAG: cir.global "private"  constant cir_private  @ref.tmp0 =
// CHECK-DAG: cir.global "private"  constant cir_private  @ref.tmp0.1 =
