// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression test:
// A direct call can resolve to a declaration created earlier with a placeholder
// `void()` function type (e.g. while parameter record types are incomplete).
// When the call site has a complete signature, we must refine the declaration
// before emitting cir.call; otherwise cir.call is created with zero
// operands/results and scalar call emission crashes.

struct Outer;
struct Sink;

Sink *createSink(Outer &, Sink *, Outer *, Sink *);

struct Sink {
  int v;
};

struct Outer {
  Sink s;
};

Sink *use(Outer &o, Sink *a, Outer *b, Sink *c) {
  // Ensure this remains a direct call with four explicit arguments and
  // a non-void scalar return.
  return createSink(o, a, b, c);
}

// CHECK-LABEL: cir.func {{.*}}@_Z3useR5OuterP4SinkPS_S2_
// CHECK: cir.call @_Z10createSinkR5OuterP4SinkPS_S2_(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) : (!cir.ptr<!rec_Outer>, !cir.ptr<!rec_Sink>, !cir.ptr<!rec_Outer>, !cir.ptr<!rec_Sink>) -> !cir.ptr<!rec_Sink>
