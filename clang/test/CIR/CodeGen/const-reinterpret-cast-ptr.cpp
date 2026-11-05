// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that const pointer globals initialized via reinterpret_cast produce
// correct types when used in expressions. The constant evaluator folds through
// reinterpret_cast, producing a GlobalViewAttr with the source type. CIR must
// insert a bitcast to match the expression's expected type.
//
//

struct A;
struct B { int x; };

static B b = {42};
static A* const p = reinterpret_cast<A*>(&b);

A* get() {
  return p;
}
// CHECK: cir.func {{.*}} @_Z3getv
// CHECK:   %[[CONST:.*]] = cir.const #cir.global_view<@_ZL1b> : !cir.ptr<!rec_A>

// Ternary with const reinterpret_cast pointer — both branches must have
// the same type (ptr<A>), even though the false branch's constant folds to
// ptr<B> and needs a bitcast.
A* test_ternary(A* q) {
  return q ? q : p;
}
// CHECK: cir.func {{.*}} @_Z12test_ternaryP1A
// CHECK:   cir.ternary
// CHECK:     cir.yield {{.*}} : !cir.ptr<!rec_A>
// CHECK:   }, false {
// CHECK:     %[[CONST2:.*]] = cir.const #cir.global_view<@_ZL1b> : !cir.ptr<!rec_A>
// CHECK:     cir.yield %[[CONST2]] : !cir.ptr<!rec_A>
