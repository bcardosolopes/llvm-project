// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// Regression coverage for incremental deferred-inline candidate tracking.
// Referencing an inline method through a declaration-only symbol must still
// keep method calls valid after release()-time deferred emission.

struct S {
  int value() const;
};

inline int S::value() const { return 42; }

int use(const S &s) { return s.value(); }

// CIR-LABEL: cir.func {{.*}} @_ZNK1S5valueEv
// CIR-LABEL: cir.func {{.*}} @_Z3useRK1S
// CIR:       cir.call @_ZNK1S5valueEv
