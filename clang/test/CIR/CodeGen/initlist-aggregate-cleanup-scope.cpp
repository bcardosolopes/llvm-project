// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// A std::initializer_list of a record aggregate (pair-like) with destructible
// fields. The backing array's element ctors share ONE cir.cleanup.scope. Its
// cleanup region destroys, on unwind:
//   1. the partial element's constructed FIELDS (cursor-relative:
//      load endOfInit + field, guarded by reused per-field flags), then
//   2. the COMPLETE elements [begin, endOfInit) via ~ElementType.
// This turns the per-call O(N^2) subobject-cleanup materialization (each ctor's
// landing pad re-destroying the accumulated prefix) into a single O(N) shared
// dispatch, the shape of a large static map. It is exception-correct: if a
// later field ctor throws, the earlier constructed field of the SAME (partial)
// element is destroyed field-wise, and earlier complete elements via their
// element destructor.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct A { A(int); ~A(); };
struct B { B(int); ~B(); };
struct Pair { A a; B b; };
struct Map { Map(std::initializer_list<Pair>); ~Map(); };

// CIR-LABEL: @_Z1fv
void f() { Map m = {{A(0), B(0)}, {A(1), B(1)}}; }

// One reused slot per destructible field of the element, naming the element
// whose copy of that field is alive.
// CIR: cir.alloca !cir.ptr<!rec_Pair>{{.*}}["field.owner"]
// CIR: cir.alloca !cir.ptr<!rec_Pair>{{.*}}["field.owner"]

// Element field ctors are bare exception calls inside one cir.cleanup.scope.
// CIR: cir.cleanup.scope {
// CIR:   cir.call exception @_ZN1AC1Ei
// CIR:   cir.call exception @_ZN1BC1Ei
// CIR:   cir.call exception @_ZN1AC1Ei
// CIR:   cir.call exception @_ZN1BC1Ei
// CIR: } cleanup {
// Partial element's fields, cursor-relative and flag-guarded, reverse order.
// CIR:   cir.if
// CIR:     cir.call @_ZN1BD1Ev
// CIR:   cir.if
// CIR:     cir.call @_ZN1AD1Ev
// Complete elements via the element (pair) destructor.
// CIR:   cir.call @_ZN4PairD1Ev
// CIR: }
