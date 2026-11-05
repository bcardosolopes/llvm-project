// RUN: %clang_cc1 -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that operator new calls get the noalias return attribute.

struct S { int x; };

int test_new() {
  S* s = new S();
  return s->x;
}

// CHECK: call noalias {{.*}} @_Znwm(

int test_new_array() {
  S* s = new S[10];
  return s[0].x;
}

// CHECK: call noalias {{.*}} @_Znam(
