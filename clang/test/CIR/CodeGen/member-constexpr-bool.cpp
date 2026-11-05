// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Test that accessing a static constexpr bool member via this->
// produces !cir.bool, not !cir.int<u, 1>. EvaluateAsInt returns a
// 1-bit APSInt for bools, which VisitMemberExpr must convert to
// !cir.bool for cir.if to accept.

struct Foo {
  static constexpr bool flag = true;
  void test() { if (this->flag) {} }
};
void call() { Foo f; f.test(); }

// CHECK: cir.func {{.*}} @_ZN3Foo4testEv
// CHECK:   %[[BOOL:.*]] = cir.const #true
// CHECK:   cir.if %[[BOOL]]
