// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir -o %t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that 'this' alignment is properly preserved during EH cleanup
// emission. When a constructor member initializer throws, the cleanup
// for already-constructed members must use the correct 'this' alignment
// (not zero).

struct Throws {
  Throws();
  ~Throws();
};

struct Inner {
  Inner();
  ~Inner();
};

struct Outer {
  Inner i;
  Throws t;
  int x = 42;
};

// The Outer constructor should invoke Throws::Throws() with an unwind
// label, and the cleanup should call Inner::~Inner() on 'this'.
// LLVM-LABEL: define {{.*}} @_ZN5OuterC2Ev
// LLVM:   invoke void @_ZN6ThrowsC1Ev(
// LLVM:   landingpad
// LLVM:     cleanup
// LLVM:   call void @_ZN5InnerD1Ev(ptr {{[^%]*}}%{{.*}})

// OGCG-LABEL: define {{.*}} @_ZN5OuterC2Ev
// OGCG:   invoke void @_ZN6ThrowsC1Ev(
// OGCG:   landingpad
// OGCG:     cleanup
// OGCG:   call void @_ZN5InnerD1Ev(ptr noundef {{.*}}%{{.*}})

void test() {
  Outer o;
}
