// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
//
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck %s --input-file=%t.og.ll --check-prefix=OGCG

// Verify that C++17 evaluation order is respected for compound assignment
// operators: the RHS is evaluated before the LHS.

struct B {
  B();
  B(const B &);
  ~B();
};

struct C {
  C();
  C(const C &);
  ~C();
};

void operator+=(B, C);

B make_b();
C make_c();

// For free-function operator+=, arguments are evaluated right-to-left per
// C++17: RHS (make_c) before LHS (make_b).
// LLVM-LABEL: define {{.*}}@_Z11free_assignv(
// LLVM: call void @_Z6make_cv(
// LLVM: call void @_Z6make_bv(

// OGCG-LABEL: define {{.*}}@_Z11free_assignv(
// OGCG: call void @_Z6make_cv(
// OGCG: call void @_Z6make_bv(
void free_assign() {
  make_b() += make_c();
}
