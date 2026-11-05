// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test EH cleanup scope: destructors are called on exception path.

struct S { ~S(); };
void may_throw();

// Basic RAII cleanup on exception path
void test_basic() {
  S s;
  may_throw();
}

// LLVM-LABEL: @{{.*}}test_basic
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM: call void @_ZN1SD1Ev(
// LLVM: resume

// OGCG-LABEL: @{{.*}}test_basic
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG: call void @_ZN1SD1Ev(
// OGCG: resume

// Nested RAII: both destructors called in reverse order
struct A { ~A(); };
struct B { ~B(); };

void test_nested() {
  A a;
  B b;
  may_throw();
}

// LLVM-LABEL: @{{.*}}test_nested
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM: call void @_ZN1BD1Ev(
// LLVM: call void @_ZN1AD1Ev(
// LLVM: resume

// OGCG-LABEL: @{{.*}}test_nested
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG: call void @_ZN1BD1Ev(
// OGCG: call void @_ZN1AD1Ev(
// OGCG: resume

// RAII inside try/catch
void test_try_catch() {
  try {
    S s;
    may_throw();
  } catch (...) {
  }
}

// LLVM-LABEL: @{{.*}}test_try_catch
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM: call void @_ZN1SD1Ev(

// OGCG-LABEL: @{{.*}}test_try_catch
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG: call void @_ZN1SD1Ev(

// Throw with exception object constructor (FreeException EH cleanup)
struct E { E(); ~E(); };

void test_throw() {
  throw E();
}

// LLVM-LABEL: @{{.*}}test_throw
// LLVM: call ptr @__cxa_allocate_exception(
// LLVM: invoke void @_ZN1EC1Ev(
// LLVM: call void @__cxa_throw(

// OGCG-LABEL: @{{.*}}test_throw
// OGCG: call ptr @__cxa_allocate_exception(
// OGCG: invoke void @_ZN1EC1Ev(
// OGCG: call void @__cxa_throw(

// RAII outside try: destructor called after catch handler completes
void test_raii_outside_try() {
  A a;
  try {
    B b;
    may_throw();
  } catch (...) {
  }
}

// Both CIR and OGCG must call ~B on EH path and ~A on normal exit.
// LLVM-LABEL: @{{.*}}test_raii_outside_try
// LLVM: invoke void @_Z9may_throwv()
// LLVM: call void @_ZN1BD1Ev(
// LLVM: call void @_ZN1AD1Ev(

// OGCG-LABEL: @{{.*}}test_raii_outside_try
// OGCG: invoke void @_Z9may_throwv()
// OGCG: call void @_ZN1BD1Ev(
// OGCG: call void @_ZN1AD1Ev(

// Throw with expression temp: __cxa_throw must be invoke.
// When throw evaluates an expression that creates temporaries (e.g.,
// `throw Error(X())`), __cxa_throw must be an invoke so ~X() runs
// during stack unwinding.
struct X { ~X(); };
struct Error { Error(const X&); };

void test_throw_with_temp() {
  throw Error(X());
}

// LLVM-LABEL: @{{.*}}test_throw_with_temp
// LLVM: call ptr @__cxa_allocate_exception(
// LLVM: invoke void @_ZN5ErrorC1ERK1X(
// LLVM: invoke void @__cxa_throw(
// LLVM: landingpad
// LLVM:   cleanup
// LLVM: call void @_ZN1XD1Ev(
// LLVM: resume

// OGCG-LABEL: @{{.*}}test_throw_with_temp
// OGCG: call ptr @__cxa_allocate_exception(
// OGCG: invoke void @_ZN5ErrorC1ERK1X(
// OGCG: invoke void @__cxa_throw(
// OGCG: call void @_ZN1XD1Ev(
// OGCG: resume

// Throw with expression temp inside RAII scope: both ~X() and ~Guard()
// must run during unwinding from __cxa_throw.
struct Guard { ~Guard(); };

void test_throw_with_temp_and_guard() {
  Guard g;
  throw Error(X());
}

// LLVM-LABEL: @{{.*}}test_throw_with_temp_and_guard
// LLVM: call ptr @__cxa_allocate_exception(
// LLVM: invoke void @_ZN5ErrorC1ERK1X(
// LLVM: invoke void @__cxa_throw(
// LLVM: landingpad
// LLVM:   cleanup
// LLVM: call void @_ZN1XD1Ev(
// LLVM: call void @_ZN5GuardD1Ev(
// LLVM: resume

// OGCG-LABEL: @{{.*}}test_throw_with_temp_and_guard
// OGCG: call ptr @__cxa_allocate_exception(
// OGCG: invoke void @_ZN5ErrorC1ERK1X(
// OGCG: invoke void @__cxa_throw(
// OGCG: call void @_ZN1XD1Ev(
// OGCG: call void @_ZN5GuardD1Ev(
// OGCG: resume
