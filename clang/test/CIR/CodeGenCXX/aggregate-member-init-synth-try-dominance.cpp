// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-mlir=cir-abi %s -o - | FileCheck %s

// Regression and: an exception-annotated aggregate-producing
// call used for a default member initializer in a constructor with active EH
// cleanups must not violate SSA dominance, and when the destination field
// address dominates the synthetic try call we should store directly instead of
// bridging through tmp.try.call.res.

struct Guard {
  Guard();
  ~Guard();
};

Guard::Guard() {}
Guard::~Guard() {}

struct Agg {
  int a;
  int b;
  int c;
  int d;
};

Agg make_agg();
Agg make_a();
Agg make_b();

struct C {
  Guard g;
  Agg q = make_agg();
  C();
};

C::C() {}

struct D {
  Guard g;
  Agg a = make_a();
  Agg b = make_b();
  D();
};

D::D() {}

// The Guard member's EH cleanup opens a cir.cleanup.scope covering the member
// initializers. The field address is computed *before* the scope: it depends
// only on `this`, so CIRGen's dominance-preserving hoist of pure ops moves it
// out. get_member only became eligible for that hoist once it stopped
// declaring a MemRead it never performed -- which is the same hoist
// below relies on.
// CHECK-LABEL: cir.func{{.*}} @_ZN1CC2Ev
// CHECK: %[[THIS:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_C>>, !cir.ptr<!rec_C>
// CHECK: %[[QADDR:.*]] = cir.get_member %[[THIS]][1] {name = "q"} : !cir.ptr<!rec_C> -> !cir.ptr<!rec_Agg>
// CHECK: cir.cleanup.scope {
// CHECK:   %[[CALL:.*]] = cir.call exception @_Z8make_aggv() : () -> !rec_anon_struct
// CHECK:   %[[QCAST:.*]] = cir.cast bitcast %[[QADDR]] : !cir.ptr<!rec_Agg> -> !cir.ptr<!rec_anon_struct>
// CHECK: } cleanup {
// CHECK-NOT: cir.copy {{.*}} to %[[QADDR]]

// Both destinations (`a` and `b`) dominate their calls because pure
// ops like get_member are hoisted ahead of the shared cleanup scope.
// CHECK-LABEL: cir.func{{.*}} @_ZN1DC2Ev
// CHECK: %[[THIS2:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_D>>, !cir.ptr<!rec_D>
// CHECK: %[[AADDR:.*]] = cir.get_member %[[THIS2]][1] {name = "a"} : !cir.ptr<!rec_D> -> !cir.ptr<!rec_Agg>
// CHECK: %[[BADDR:.*]] = cir.get_member %[[THIS2]][2] {name = "b"} : !cir.ptr<!rec_D> -> !cir.ptr<!rec_Agg>
// CHECK: cir.cleanup.scope {
// CHECK:   %[[CALLA:.*]] = cir.call exception @_Z6make_av() : () -> !rec_anon_struct
// CHECK:   %[[ACAST:.*]] = cir.cast bitcast %[[AADDR]] : !cir.ptr<!rec_Agg> -> !cir.ptr<!rec_anon_struct>
// CHECK:   %[[CALLB:.*]] = cir.call exception @_Z6make_bv(){{.*}} : () -> !rec_anon_struct
// CHECK:   %[[BCAST:.*]] = cir.cast bitcast %[[BADDR]] : !cir.ptr<!rec_Agg> -> !cir.ptr<!rec_anon_struct>
// CHECK: } cleanup {
