// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// An initializer_list's element constructors are wrapped in a
// cir.cleanup.scope built for them directly, whose region destroys the
// elements built so far. The objects already alive around the list are not in
// that region: the dispatch stops at the list's own EH depth, on the
// assumption that a scope formed around this one carries them and FlattenCFG
// chains this pad into it. A list initializing a plain local has no such
// scope -- it stands alone in its block, with only siblings before and after
// it -- and a pad chains outward, never sideways, so those destructors ran
// nowhere and the objects leaked.

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
  W b{U(2), U(3)};
  U c(4);
}

// CIR-LABEL: cir.func {{.*}}@_Z1fv()
// CIR:   %[[A:.*]] = cir.alloca !rec_U, {{.*}} ["a", init]
// CIR:   %[[B:.*]] = cir.alloca !rec_W, {{.*}} ["b", init]
// CIR:   cir.call @_ZN1UC1Ei(%[[A]],

// Constructing the elements. Unwinding out of either constructor destroys the
// elements already built -- the cursor-driven loop -- and then `a`, which is
// live across the whole list.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1UC1Ei
// CIR:     cir.call exception @_ZN1UC1Ei
// CIR:     cir.yield
// CIR-NEXT: } cleanup {
// CIR:     cir.do {
// CIR:       cir.call @_ZN1UD1Ev(
// CIR:     } while {

// Then `a`, and nothing else. The partial-array cleanup this region wrote the
// loop for pops like any other one, and appending it here as well -- it is
// still on the stack when it does -- would put a second loop in front of `a`
// and destroy every element twice on this edge.
// CIR-NOT: cir.do {
// CIR:     cir.call @_ZN1UD1Ev(%[[A]])
// CIR-NEXT: cir.yield

// Handing the list to `W`: the array is complete now, so its destruction is
// the flag-guarded whole-array cleanup, and `a` is still live behind it.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN1WC1ESt16initializer_listI1UE(%[[B]],
// CIR:     cir.yield
// CIR-NEXT: } cleanup {
// CIR:     cir.call @_ZN1UD1Ev(%[[A]])
// CIR-NEXT: cir.yield
