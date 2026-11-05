// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fmerge-all-constants -fclangir -emit-cir %s -o - | FileCheck %s

struct E {};

// User-provided symbol that deliberately collides with the compiler-generated
// ref.tmp naming base.
extern "C" int user_named_ref_tmp0 asm("ref.tmp0") = 7;

int sink(const E &e = {}) {
  (void)e;
  return 1;
}

int f() { return sink(); }

// Ensure merged-constant temporary naming avoids colliding with an existing
// module symbol named ref.tmp0.
// CHECK-DAG: cir.global external @ref.tmp0
// CHECK-DAG: cir.global "private"  constant cir_private  @ref.tmp0.1 =
