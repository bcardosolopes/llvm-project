// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Verify that multi-dimensional array initialization with exception cleanup
// correctly bitcasts outer element pointers to base element pointers in the
// IrregularPartialArrayDestroy cleanup. Previously this produced a
// cir.store type mismatch because the outer endOfInit pointer
// (!cir.ptr<!cir.array<!rec_A x 3>>) was stored into a cursor alloca
// expecting !cir.ptr<!rec_A>.

struct A {
  A(unsigned i);
  ~A();
};

void test_2d() {
  A v[2][3] = { { A(0), A(1), A(2) }, { A(3), A(4), A(5) } };
}

// CHECK: cir.func {{.*}} @_Z7test_2dv
// CHECK:   %[[ALLOCA:.*]] = cir.alloca !cir.array<!cir.array<!rec_A x 3> x 2>

// Outer endOfInit alloca stores subarray pointers.
// CHECK:   %[[OUTER_EOI:.*]] = cir.alloca !cir.ptr<!cir.array<!rec_A x 3>>, !cir.ptr<!cir.ptr<!cir.array<!rec_A x 3>>>

// Outer begin via array_to_ptrdecay.
// CHECK:   %[[OUTER_BEGIN:.*]] = cir.cast array_to_ptrdecay %[[ALLOCA]] : !cir.ptr<!cir.array<!cir.array<!rec_A x 3> x 2>> -> !cir.ptr<!cir.array<!rec_A x 3>>

// In the outer cleanup, the endOfInit load is bitcast to the base element type
// before use in the destruction loop.
// CHECK:   cir.cast bitcast %[[OUTER_BEGIN]] : !cir.ptr<!cir.array<!rec_A x 3>> -> !cir.ptr<!rec_A>
// CHECK:   cir.cast bitcast %{{.*}} : !cir.ptr<!cir.array<!rec_A x 3>> -> !cir.ptr<!rec_A>
