// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir \
// RUN:    -emit-cir -o /dev/null %s 2>&1 | FileCheck %s
//
// CIR does not yet handle conditional operator results that are bitfield
// lvalues. Verify it emits a proper NYI diagnostic.

// CHECK: Not Yet Implemented: conditional operator: non-simple lvalue
struct S {
  unsigned a : 4;
  unsigned b : 4;
};

void test(bool cond, S &s, unsigned val) {
  (cond ? s.a : s.b) = val;
}
