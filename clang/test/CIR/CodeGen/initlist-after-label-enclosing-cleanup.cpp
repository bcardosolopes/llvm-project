// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// The enclosing destructors appended to an initializer_list's cir.cleanup.scope
// are kept only if the scope op is reached by everything the appended cleanups
// name. That test walked out from the scope op comparing blocks for identity,
// so a value defined in a block that merely DOMINATES the one the scope op sits
// in read as out of reach. A label puts one there: the statement after it
// starts a new block, and everything declared before the label was defined in
// the previous one. The append was then rolled back and `a` leaked on the
// unwind edge out of the element constructors -- with the label removed, the
// same function destroyed it.

namespace std {
template <class E> class initializer_list {
  const E *b_;
  decltype(sizeof(0)) s_;

public:
  initializer_list() : b_(nullptr), s_(0) {}
};
} // namespace std

struct U {
  U(int);
  ~U();
};
struct W {
  W(std::initializer_list<U>);
  ~W();
};

void f() {
  U a(1);
L:
  W b{U(2), U(3)};
}

// CIR-LABEL: cir.func {{.*}}@_Z1fv()
// CIR:   %[[A:.*]] = cir.alloca !rec_U, {{.*}} ["a", init]
// CIR:   cir.call @_ZN1UC1Ei(%[[A]],
// CIR:   cir.br ^bb1
// CIR: ^bb1:
// CIR:   cir.label "L"

// Unwinding out of either element constructor destroys the elements built so
// far, and then `a`, which is live across the whole list and is defined back in
// the block the label branched out of.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1UC1Ei
// CIR:     cir.call exception @_ZN1UC1Ei
// CIR:     cir.yield
// CIR-NEXT: } cleanup {
// CIR:     cir.do {
// CIR:       cir.call @_ZN1UD1Ev(
// CIR:     } while {
// CIR:     cir.call @_ZN1UD1Ev(%[[A]])
// CIR-NEXT: cir.yield

// Handing the completed list to `W` leaves `a` behind it on that edge too.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1WC1ESt16initializer_listI1UE
// CIR:     cir.yield
// CIR-NEXT: } cleanup {
// CIR:     cir.call @_ZN1UD1Ev(%[[A]])
