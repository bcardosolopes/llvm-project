// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu++20 \
// RUN:   -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu++20 \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression: local reference temporaries (ref.tmpN) in functions
// with parameters must not be marked as debug arguments.
//
// Previously these could be assigned arg: 1 in DILocalVariable metadata,
// causing verifier failures ("conflicting debug info for argument").
struct A {
  A();
  A(const A &);
  ~A();
};

void take(const A &);

void f(const A &x) {
  take(x);
  take((A()));
  take((A()));
  take((A()));
}

// CIR: !DILocalVariable(name: "x", arg: 1
// CIR-NOT: !DILocalVariable(name: "ref.tmp{{[0-9]*}}", arg:

// OGCG: !DILocalVariable(name: "x", arg: 1
// OGCG-NOT: !DILocalVariable(name: "ref.tmp{{[0-9]*}}", arg:
