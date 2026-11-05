// RUN: %clang_cc1 -std=c++2c -triple x86_64-pc-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++2c -triple x86_64-pc-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++2c -triple x86_64-pc-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Test for C++26 structured bindings as condition variables (P0963R3).
// The holding vars (get<I>() results) must be emitted AFTER the condition
// is evaluated (operator bool) but BEFORE the statement body.

namespace std {
template <typename T> struct tuple_size;
template <int, typename> struct tuple_element;
}

struct S {
  int a, b;
  constexpr explicit operator bool() { return a != b; }
  template <int I> constexpr int &get() { return I == 0 ? a : b; }
};

template <> struct std::tuple_size<S> { static const int value = 2; };
template <int I> struct std::tuple_element<I, S> { using type = int; };

// --- if statement ---

void test_if() {
  if (auto [a, b] = S(1, 2)) {
    (void)a;
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z7test_ifv()
// CIR: cir.if
// CIR: cir.return

// Verify call ordering: operator bool BEFORE get<0>/get<1>.
// LLVM-LABEL: define {{.*}} @_Z7test_ifv()
// LLVM: call {{.*}} @_ZN1ScvbEv
// LLVM: call {{.*}} @_ZN1S3getILi0EEERiv
// LLVM: call {{.*}} @_ZN1S3getILi1EEERiv

// OGCG-LABEL: define {{.*}} @_Z7test_ifv()
// OGCG: call {{.*}} @_ZN1ScvbEv
// OGCG: call {{.*}} @_ZN1S3getILi0EEERiv
// OGCG: call {{.*}} @_ZN1S3getILi1EEERiv

// --- while statement ---

void test_while() {
  while (auto [a, b] = S(1, 1)) {
    break;
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z10test_whilev()
// CIR: cir.while
// CIR: cir.return

// LLVM-LABEL: define {{.*}} @_Z10test_whilev()
// LLVM: call {{.*}} @_ZN1ScvbEv
// LLVM: call {{.*}} @_ZN1S3getILi0EEERiv
// LLVM: call {{.*}} @_ZN1S3getILi1EEERiv

// OGCG-LABEL: define {{.*}} @_Z10test_whilev()
// OGCG: call {{.*}} @_ZN1ScvbEv
// OGCG: call {{.*}} @_ZN1S3getILi0EEERiv
// OGCG: call {{.*}} @_ZN1S3getILi1EEERiv

// --- for statement ---

void test_for() {
  for (; auto [a, b] = S(1, 1);) {
    break;
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z8test_forv()
// CIR: cir.for
// CIR: cir.return

// LLVM-LABEL: define {{.*}} @_Z8test_forv()
// LLVM: call {{.*}} @_ZN1ScvbEv
// LLVM: call {{.*}} @_ZN1S3getILi0EEERiv
// LLVM: call {{.*}} @_ZN1S3getILi1EEERiv

// OGCG-LABEL: define {{.*}} @_Z8test_forv()
// OGCG: call {{.*}} @_ZN1ScvbEv
// OGCG: call {{.*}} @_ZN1S3getILi0EEERiv
// OGCG: call {{.*}} @_ZN1S3getILi1EEERiv

// --- switch statement ---

struct T {
  int a, b;
  constexpr operator int() { return a + b; }
  template <int I> constexpr int &get() { return I == 0 ? a : b; }
};

template <> struct std::tuple_size<T> { static const int value = 2; };
template <int I> struct std::tuple_element<I, T> { using type = int; };

void test_switch() {
  switch (auto [a, b] = T{1, 2}) {
  case 3:
    (void)a;
    break;
  default:
    break;
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z11test_switchv()
// CIR: cir.switch
// CIR: cir.return

// Verify call ordering: operator int BEFORE get<0>/get<1>.
// LLVM-LABEL: define {{.*}} @_Z11test_switchv()
// LLVM: call {{.*}} @_ZN1TcviEv
// LLVM: call {{.*}} @_ZN1T3getILi0EEERiv
// LLVM: call {{.*}} @_ZN1T3getILi1EEERiv

// OGCG-LABEL: define {{.*}} @_Z11test_switchv()
// OGCG: call {{.*}} @_ZN1TcviEv
// OGCG: call {{.*}} @_ZN1T3getILi0EEERiv
// OGCG: call {{.*}} @_ZN1T3getILi1EEERiv
