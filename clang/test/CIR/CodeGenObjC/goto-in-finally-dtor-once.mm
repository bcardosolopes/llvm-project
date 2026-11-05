// RUN: %clang_cc1 -triple arm64-apple-macosx11.0.0 -fobjc-runtime=macosx-11.0.0 -fexceptions -fobjc-exceptions -fobjc-arc -std=c++17 -x objective-c++ -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// A forward goto over a local with a non-trivial destructor, inside an ObjC
// @finally body, must destroy that local EXACTLY ONCE in the exceptional
// (unwind) copy of the @finally -- at the single scope exit past the label, not
// on the goto edge as well. The @finally unwind copy is emitted with
// useCurrentScope=true directly under a LexicalScope (handlerScope) that had no
// clang source range; emitGotoStmt decides which scopes a forward goto exits by
// comparing the label location against each scope's source range, so the
// missing range made the goto appear to exit handlerScope (owner of the local's
// cleanup) and ran the destructor on the goto edge in addition to the normal
// exit -> double free. Sibling of the goto/cleanup source-range fixes in
// goto-enclosing-scope-dtor-once.cpp.

struct Guard {
  Guard();
  ~Guard();
};

extern "C" void mayThrow();

// CHECK-LABEL: cir.func {{.*}}@_Z9f_nocatchv
void f_nocatch() {
  @try {
    mayThrow();
  } @finally {
    Guard g;
    goto done;
  done:;
  }
}

// The exceptional finally copy lives in the catch-all handler. It constructs g,
// then reaches the goto -- with NO destructor on the goto edge.
// CHECK:      } catch all {
// CHECK:        cir.call exception @_ZN5GuardC1Ev
// CHECK-NOT:    @_ZN5GuardD1Ev
// CHECK:        cir.goto "done"
// After the label, ~Guard runs exactly once, in the rethrow cleanup.
// CHECK:        cir.label "done"
// CHECK:        cir.call exception @objc_exception_rethrow
// CHECK:          cir.call @_ZN5GuardD1Ev
