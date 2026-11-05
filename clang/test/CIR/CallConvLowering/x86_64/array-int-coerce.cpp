// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Wrapper arrays should not block x86_64 INTEGER-chunk narrowing. For
// struct Outer { Inner x[1]; } with Inner = { double, int }, the high
// eightbyte must recurse through the array and preserve the inner i32,
// matching OGCG instead of widening back to i64.

struct Inner {
  double x;
  int y;
};

struct Outer {
  Inner x[1];
};

// CIR: define{{.*}} @_Z1f5Outer(double %{{.*}}, i32 %{{.*}})
// OGCG: define{{.*}} @_Z1f5Outer(double %{{.*}}, i32 %{{.*}})
void f(Outer v) {}

