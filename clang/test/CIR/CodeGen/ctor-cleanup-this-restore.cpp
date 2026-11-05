// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that EH cleanup emission restores the correct 'this' pointer when
// emitting CallBaseDtor cleanups inside nested CXXDefaultInitExprScopes.
// Previously, CIR's eager cleanup emission would use a 'this' pointer that
// had been temporarily modified by CXXDefaultInitExprScope to point to a
// member field instead of the derived class, causing an assertion failure
// in getAddressOfDirectBaseInCompleteClass.

struct Base {
  virtual ~Base();
};

struct Inner {
  Inner();
  ~Inner();
};

struct Aggregate {
  int x;
  Inner i = {};
};

struct Derived : Base {
  Aggregate agg = {};
};

// The base destructor in the cleanup region must address the *Derived* object,
// not the member field 'this' was pointing at while the initializer was built.
// CIR-LABEL: @_ZN7DerivedC2Ev
// CIR: cir.call @_ZN4BaseC2Ev
// CIR: cir.cleanup.scope {
// CIR:   cir.call exception @_ZN5InnerC1Ev
// CIR: } cleanup {
// CIR:   %[[BASE:[0-9]+]] = cir.base_class_addr %{{[0-9]+}} : !cir.ptr<!rec_Derived> nonnull [0] -> !cir.ptr<!rec_Base>
// CIR:   cir.call @_ZN4BaseD2Ev(%[[BASE]])

// LLVM-LABEL: define{{.*}} void @_ZN7DerivedC2Ev
// LLVM: invoke void @_ZN5InnerC1Ev
// LLVM: landingpad
// LLVM:   cleanup
// LLVM: call void @_ZN4BaseD2Ev

void test() {
  Derived d;
}
