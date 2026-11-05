// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// An initializer-list array with several non-trivially-destructible elements
// must share ONE partial-array-cleanup landing pad across all the element
// constructors, rather than emitting a separate synthetic try + full
// partial-array-destroy loop per element.
//
// Regression test: when each element ctor got its own cleanup copy, a
// store-forwarding pass specialized each copy's endOfInit load to that
// element's constant end pointer, so the copies were no longer identical and
// could not be folded; LLVM then unrolled each to its constant trip count
// (0, 1, ..., N-1), producing O(N^2) exception-cleanup code. With a
// single shared landing pad the endOfInit load has N reaching stores, stays
// runtime-valued, and remains one loop -> O(N), matching classic Clang.

struct S {
  int x;
  S(int);
  ~S();
};

void sink(S *);

// CIR-LABEL: @_Z1fv
void f() {
  S a[] = {S(0), S(1), S(2), S(3), S(4)};
  sink(a);
}

// The partial-array cleanup is tracked by a single endOfInit cursor.
// CIR:      cir.alloca {{.*}} ["arrayinit.endOfInit"]

// All five element ctors live under ONE synthetic cleanup try: the first
// carries the canonical partial-array cleanup region, and the remaining four
// reuse it via cir.share_synthetic_cleanup (empty cleanup regions), i.e. they
// share the single landing pad instead of cloning the destroy loop. Without the
// fix each ctor got its own synthetic try + full cleanup copy (O(N^2) code).
// CIR:      cir.try synthetic cleanup {
// CIR:        cir.call exception @_ZN1SC1Ei({{.*}}) : {{.*}} cleanup {
// CIR:        cir.call exception @_ZN1SC1Ei({{.*}}) {cir.share_synthetic_cleanup}
// CIR:        cir.call exception @_ZN1SC1Ei({{.*}}) {cir.share_synthetic_cleanup}
// CIR:        cir.call exception @_ZN1SC1Ei({{.*}}) {cir.share_synthetic_cleanup}
// CIR:        cir.call exception @_ZN1SC1Ei({{.*}}) {cir.share_synthetic_cleanup}
