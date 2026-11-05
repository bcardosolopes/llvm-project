// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression for no-proto rewrite mismatch handling with multiple pointer
// mismatches in one call site.

struct A;
struct B;
int np_two_ptr();

int use_two_ptr(void *a, void *b) { return np_two_ptr(a, b); }

struct A {
  int x;
};
struct B {
  int y;
};
int np_two_ptr(struct A *a, struct B *b) { return a->x + b->y; }

// CHECK-LABEL: define dso_local i32 @use_two_ptr(
// CHECK: call i32 @np_two_ptr(ptr noundef
