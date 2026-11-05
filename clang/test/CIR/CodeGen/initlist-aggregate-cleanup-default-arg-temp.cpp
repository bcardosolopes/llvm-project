// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// An initializer_list of a record aggregate whose field constructor takes a
// defaulted argument that is itself a temporary with a destructor. This is the
// shape std::vector has: its initializer_list constructor's `const allocator&`
// parameter is defaulted, so building `struct M { std::vector<int> a; }` from
// `{{1, 2}}` materializes an allocator temporary per element.
//
// Those temporaries live to the end of the full expression, so their cleanups
// sit ABOVE the ones the array init pushed (the complete-element cleanup A and
// the per-field cleanups). Deactivating "the top N cleanups" after the element
// ctors pops the temporaries instead and leaves A and the field cleanups
// active, so they also run on the normal path and destroy elements the backing
// array's own cleanup already destroyed. Deactivate the exact scopes pushed.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct Alloc { ~Alloc(); };
struct A { A(int, const Alloc & = Alloc()); ~A(); };
struct M { A a; };
void use(std::initializer_list<M>);

void t() { use({{1}, {2}}); }

// CIR-LABEL: cir.func {{.*}}@_Z1tv
//       CIR:   %[[FIELD:.*]] = cir.alloca !cir.ptr<!rec_M>, {{.*}}["field.owner"]

// The two element ctors share one cleanup scope, whose cleanup region destroys
// the partial element's field and then the complete elements.
//       CIR:   cir.cleanup.scope {
//       CIR:     cir.call exception @_ZN1AC1EiRK5Alloc
//       CIR:     cir.call exception @_ZN1AC1EiRK5Alloc
//       CIR:   } cleanup {
//       CIR:     cir.call @_ZN1AD1Ev
//       CIR:     cir.call @_ZN1MD1Ev

// Both cleanups this array init pushed are stopped once the last element is
// complete: the field's slot is emptied and the complete-element cleanup A's
// flag goes false. The stores land right before the scope holding the `use`
// call.
//       CIR:   cir.store align(8) %{{.+}}, %[[FIELD]] : !cir.ptr<!rec_M>
//  CIR-NEXT:   cir.store align(1) %{{.+}}, %{{.+}} : !cir.bool
//  CIR-NEXT:   cir.store align(1) %{{.+}}, %{{.+}} : !cir.bool
//  CIR-NEXT:   cir.cleanup.scope {
//       CIR:     cir.call exception @_Z3useSt16initializer_listI1ME

// Unwinding out of `use` destroys the backing array, then everything the two
// elements built, in reverse of the order they were built in: the second
// element's field sits above its own allocator temporary and below the first
// element's, so the two ~A's interleave with the two ~Alloc's. Both of these
// cleanups are deactivated by now, so their flags are false and none of them
// runs; the order is what the same dispatch does on the edges inside the
// initialization, where they are live.
//       CIR:     cir.call @_ZN1MD1Ev
//       CIR:     cir.call @_ZN5AllocD1Ev
//       CIR:     cir.call @_ZN1AD1Ev
//       CIR:     cir.call @_ZN5AllocD1Ev
//       CIR:     cir.call @_ZN1MD1Ev

// Normal path: destroy the backing array exactly once, then the two temporaries.
// A second array destroy here would destroy every element twice, and a `~A`
// here would do the same to the last element's field.
//       CIR:   cir.call @_ZN1MD1Ev
//       CIR:   cir.call @_ZN5AllocD1Ev
//       CIR:   cir.call @_ZN5AllocD1Ev
//   CIR-NOT:   cir.call @_ZN1MD1Ev
//   CIR-NOT:   cir.call @_ZN1AD1Ev
//       CIR:   cir.return
