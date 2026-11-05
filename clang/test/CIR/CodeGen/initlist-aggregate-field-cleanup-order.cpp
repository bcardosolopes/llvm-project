// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// An initializer_list of a record aggregate whose fields are built in place.
// Each field's cleanup has to sit where the field was built: an unwind out of
// the initialization destroys everything in reverse of the order it was
// constructed in, and each field's initializer leaves a temporary of its own
// behind, alive to the end of the full expression. Hoisting one cleanup per
// field ahead of the whole body puts every field below every temporary, so
// the fields come out last instead of interleaved.
//
// One cleanup per (element, field), then, and one slot per FIELD holding the
// element whose copy of that field is alive -- `owner == &array[i]` is both
// "this field was built" and "this is my element", so a copy runs exactly on
// the unwind edges of the element it was pushed for, and running it empties
// the slot so no other copy of the field can run too.

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
struct B { B(int, const Alloc & = Alloc()); ~B(); };
struct M { A a; B b; };
void use(std::initializer_list<M>);

void t() { use({{1, 2}, {3, 4}}); }

// Two destructible fields, so two slots -- however many elements there are.
// The one bool is what the cleanup machinery deactivates through; the cleanups
// themselves do not read it.
// CIR-LABEL: cir.func {{.*}}@_Z1tv
//       CIR:   cir.alloca !cir.ptr<!rec_M>, {{.*}}["field.owner"]
//       CIR:   cir.alloca !cir.bool, {{.*}}["field.cond"]
//       CIR:   cir.alloca !cir.ptr<!rec_M>, {{.*}}["field.owner"]
//   CIR-NOT:   ["field.owner"]
//   CIR-NOT:   ["field.cond"]
//       CIR:   cir.scope {
//         CIR:   %[[BEGIN:.+]] = cir.cast array_to_ptrdecay
//         CIR:   cir.store align(8) %[[BEGIN]], %[[CURSOR:.+]] :

// Each field is announced where it is built, right after its own constructor,
// by naming the element it belongs to.
//       CIR:   cir.cleanup.scope {
//       CIR:     cir.call exception @_ZN1AC1EiRK5Alloc
//  CIR-NEXT:     cir.store align(8) %[[BEGIN]], %[[OA:.+]] : !cir.ptr<!rec_M>
//       CIR:     cir.call exception @_ZN1BC1EiRK5Alloc
//  CIR-NEXT:     cir.store align(8) %[[BEGIN]], %[[OB:.+]] : !cir.ptr<!rec_M>

// The element is complete, so the array cleanup covers it and the slots are
// emptied before the next element starts.
//       CIR:     %[[ELT1:.+]] = cir.ptr_stride inbounds %[[BEGIN]]
//       CIR:     cir.store align(8) %[[ELT1]], %[[CURSOR]] :
//       CIR:     %[[N1:.+]] = cir.const #cir.ptr<null>
//  CIR-NEXT:     cir.store align(8) %[[N1]], %[[OA]] :
//       CIR:     %[[N2:.+]] = cir.const #cir.ptr<null>
//  CIR-NEXT:     cir.store align(8) %[[N2]], %[[OB]] :
//       CIR:     cir.call exception @_ZN1AC1EiRK5Alloc
//  CIR-NEXT:     cir.store align(8) %[[ELT1]], %[[OA]] :

// Unwinding runs the second element's fields interleaved with the temporaries
// their initializers made, then the first element's, then the array cleanup
// over the complete elements. A hoisted cleanup per field would put both ~B
// and ~A after all four ~Alloc.
//
// The second element's copies are the ones comparing against `begin + 1`, the
// first element's against `begin` itself; whichever copy destroys the field
// empties the slot, so the other cannot destroy it again.
//       CIR:   } cleanup {
//       CIR:     %[[C1:.+]] = cir.load align(8) %[[OB]]
//       CIR:     %[[E1:.+]] = cir.ptr_stride inbounds %[[BEGIN]]
//       CIR:     %[[EQ1:.+]] = cir.cmp(eq, %[[C1]], %[[E1]])
//       CIR:     cir.if %[[EQ1]] {
//       CIR:       cir.call @_ZN1BD1Ev
//       CIR:       cir.store align(8) %{{.+}}, %[[OB]] : !cir.ptr<!rec_M>
//       CIR:     cir.call @_ZN5AllocD1Ev
//       CIR:     %[[C2:.+]] = cir.load align(8) %[[OA]]
//       CIR:     %[[E2:.+]] = cir.ptr_stride inbounds %[[BEGIN]]
//       CIR:     %[[EQ2:.+]] = cir.cmp(eq, %[[C2]], %[[E2]])
//       CIR:     cir.if %[[EQ2]] {
//       CIR:       cir.call @_ZN1AD1Ev
//       CIR:       cir.store align(8) %{{.+}}, %[[OA]] : !cir.ptr<!rec_M>
//       CIR:     cir.call @_ZN5AllocD1Ev
//       CIR:     %[[C3:.+]] = cir.load align(8) %[[OB]]
//       CIR:     %[[EQ3:.+]] = cir.cmp(eq, %[[C3]], %[[BEGIN]])
//       CIR:     cir.if %[[EQ3]] {
//       CIR:       cir.call @_ZN1BD1Ev
//       CIR:     cir.call @_ZN5AllocD1Ev
//       CIR:     %[[C4:.+]] = cir.load align(8) %[[OA]]
//       CIR:     %[[EQ4:.+]] = cir.cmp(eq, %[[C4]], %[[BEGIN]])
//       CIR:     cir.if %[[EQ4]] {
//       CIR:       cir.call @_ZN1AD1Ev
//       CIR:     cir.call @_ZN5AllocD1Ev
//       CIR:     cir.call @_ZN1MD1Ev

// Normal path: the array is complete, so both slots are emptied and a dispatch
// chaining to these copies finds nothing to do.
//       CIR:   cir.store align(8) %{{.+}}, %[[OA]] : !cir.ptr<!rec_M>
//  CIR-NEXT:   cir.store align(8) %{{.+}}, %[[OB]] : !cir.ptr<!rec_M>
