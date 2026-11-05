// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// CIR omits the destructor call for temporary objects used in throw
// expressions. When X() is constructed and passed by reference to Error's ctor,
// the temporary must be destroyed after the throw — CIR never calls ~X().

struct X {
  ~X();
};

struct Error {
  Error(const X &);
};

// The temporary X must be cleaned up. Both CIR and OGCG call _ZN1XD1Ev
// in the cleanup landing pad.

// CHECK-LABEL: @_Z1fv
// CHECK: invoke void @_ZN5ErrorC1ERK1X(
// CHECK: invoke void @__cxa_throw(
// CHECK: call void @_ZN1XD1Ev(

// OGCG-LABEL: @_Z1fv
// OGCG: invoke void @_ZN5ErrorC1ERK1X(
// OGCG: invoke void @__cxa_throw(
// OGCG: call void @_ZN1XD1Ev(
void f() {
  throw Error(X());
}
