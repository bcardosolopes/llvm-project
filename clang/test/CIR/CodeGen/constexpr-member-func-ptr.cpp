// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s -check-prefix=OGCG

// Previously expected to fail: Member function pointers with base class
// conversion. Both CIR and OGCG produce matching output.

struct B1 {
  void g();
};

struct B2 {
  char pad[24];
  void i();
};

struct D : B1, B2 {};

typedef void (D::*MFP)();

MFP dg = &B1::g;
MFP di = &B2::i;

// CIR: @dg = {{.*}}global { i64, i64 } { i64 ptrtoint (ptr @_ZN2B11gEv to i64), i64 0 }
// CIR: @di = {{.*}}global { i64, i64 } { i64 ptrtoint (ptr @_ZN2B21iEv to i64), i64 0 }
// OGCG: @dg = global { i64, i64 } { i64 ptrtoint (ptr @_ZN2B11gEv to i64), i64 0 }
// OGCG: @di = global { i64, i64 } { i64 ptrtoint (ptr @_ZN2B21iEv to i64), i64 0 }
