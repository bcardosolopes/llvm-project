// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// Regression coverage: deferred emission path carries precomputed
// mangled names in the queue. Ensure deferred inline special members are still
// emitted and referenced correctly.

struct Member {
  Member();
  ~Member();
};

struct Holder {
  Member m;
  Holder() = default;
  ~Holder() = default;
};

void sink() {
  Holder h;
}

// CIR-LABEL: cir.func {{.*}} @_ZN6HolderC1Ev
// CIR:       cir.call @_ZN6HolderC2Ev
// CIR-LABEL: cir.func {{.*}} @_ZN6HolderD1Ev
// CIR:       cir.call @_ZN6HolderD2Ev
// CIR-LABEL: cir.func {{.*}} @_Z4sinkv
// CIR:       cir.call @_ZN6HolderC1Ev
// CIR:       cir.call @_ZN6HolderD1Ev
