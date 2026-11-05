// RUN: %clang_cc1 -fclangir -emit-llvm -std=c++20 -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that this pointer in constructors with virtual bases uses
// non-virtual alignment, not the full type alignment.

struct A {
  __attribute__((aligned(16))) double data1;
  virtual ~A() = default;
};
struct B : public virtual A {
  B();
  double a;
  int x;
};
B::B() : x(123) {}

// B's non-virtual alignment is 8, not 16 (A's alignment is via virtual base).
// CHECK: define {{.*}} @_ZN1BC2Ev(ptr noundef nonnull align 8
// CHECK: define {{.*}} @_ZN1BC1Ev(ptr noundef nonnull align 8
