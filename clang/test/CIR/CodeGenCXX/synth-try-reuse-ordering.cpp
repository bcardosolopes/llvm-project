// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o - | FileCheck %s

// Regression test: two throwing calls at the same EH cleanup depth with a
// non-throwing side-effecting operation (store) between them.  They share one
// cir.cleanup.scope, and the intervening store must stay where it is --
// previously the second call was hoisted into the first TryOp, placing it
// before the store.

struct Guard {
  Guard();
  ~Guard();
};

Guard::Guard() {}
Guard::~Guard() {}

int may_throw_a();
int may_throw_b();

struct S {
  Guard g;
  int x;
  int y;
  S();
};

// The constructor has a Guard member whose destructor acts as an EH cleanup.
// Both may_throw_a() and may_throw_b() unwind to that cleanup.  The store to
// `x` between them is a side-effecting operation that must not be reordered.
S::S() : x(may_throw_a()), y(may_throw_b()) {}

// CHECK-LABEL: cir.func{{.*}} @_ZN1SC2Ev
//
// Both calls live in the one cleanup scope the Guard member creates.
// The member addresses are computed before the scope, not inside it. CIRGen
// hoists pure ops ahead of a synthetic try to keep dominance, and get_member
// only became eligible once it stopped declaring a MemRead it never performed.
// Where an address is computed is immaterial; the ordering this test exists to
// pin is the stores and the calls, asserted below.
// CHECK:       %[[X:.*]] = cir.get_member %{{.*}}[1] {name = "x"}
// CHECK:       cir.cleanup.scope {
// CHECK:         cir.call exception @_Z11may_throw_av()
//
// The store to the 'x' member must happen before the second call.
// CHECK:         cir.store align(4) %{{.*}}, %[[X]]
//
// CHECK:         cir.call exception @_Z11may_throw_bv()
// CHECK:         cir.yield
// CHECK:       } cleanup {
// CHECK:         cir.call @_ZN5GuardD1Ev
