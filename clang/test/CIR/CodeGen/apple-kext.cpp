// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fapple-kext -fclangir \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fapple-kext -fclangir \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that -fapple-kext correctly emits vtable dispatch for virtual
// destructor calls and emits vtable/RTTI globals with internal linkage.

struct Base {
  virtual ~Base();
};

struct Derived : Base {
  void operator delete(void *) {}
  Derived();
};

void foo() {
  Derived d1;
}

// The destructor call for d1 should go through the vtable, not be a direct
// call to _ZN7DerivedD1Ev.
// CIR-LABEL: @_Z3foov
// CIR: cir.call @_ZN7DerivedC1Ev
// CIR: cir.get_global @_ZTV7Derived
// CIR: cir.load
// CIR: cir.call

// Vtable should have internal linkage in kext mode.
// LLVM: @_ZTV7Derived = internal unnamed_addr constant

// LLVM-LABEL: define void @_Z3foov
// LLVM: call void @_ZN7DerivedC1Ev
// LLVM: load ptr, ptr getelementptr inbounds
// LLVM: call void %

inline unsigned f(unsigned n) { return n == 0 ? 0 : n + f(n-1); }
unsigned g(unsigned n) { return f(n); }

template <typename X> X ident(X x) { return x; }
int foo(int n) { return ident(n); }

// Inline function should have internal linkage.
// LLVM: define internal{{.*}} @_Z1fj

// Implicit template instantiation should have internal linkage.
// LLVM: define internal{{.*}} @_Z5identIiET_S0_

// Complete destructor should have internal linkage.
// LLVM: define internal void @_ZN7DerivedD1Ev

// operator delete shim should be emitted.
// LLVM: define internal void @_ZN7DeriveddlEPv

// Deleting destructor should be emitted.
// LLVM: define internal void @_ZN7DerivedD0Ev
