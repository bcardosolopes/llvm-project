// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// Regression for / follow-ups:
// Ensure deferred emission remains behavior-preserving when a TU mixes
// (1) template instantiation bodies and (2) deferred inline special members.

template <typename T>
T add1(T v) {
  return v + 1;
}

struct S {
  int value;
  S();
  ~S();
};

inline S::S() : value(add1<int>(41)) {}
inline S::~S() {}

struct Holder {
  S s;
  Holder() = default;
  ~Holder() = default;
};

int use() {
  Holder h;
  return h.s.value;
}

// CIR-LABEL: cir.func {{.*}} @_Z4add1IiET_S0_
// CIR-LABEL: cir.func {{.*}} @_ZN6HolderC1Ev
// CIR:       cir.call @_ZN6HolderC2Ev
// CIR-LABEL: cir.func {{.*}} @_ZN6HolderD1Ev
// CIR:       cir.call @_ZN6HolderD2Ev
// CIR-LABEL: cir.func {{.*}} @_Z3usev
// CIR:       cir.call @_ZN6HolderC1Ev
// CIR:       cir.call @_ZN6HolderD1Ev
