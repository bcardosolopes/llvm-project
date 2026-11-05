// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression tests for inherited struct ABI coercion on x86_64.

// Case 1: {float, double} struct — field offset classification.
// The float occupies the low 32 bits of the first eightbyte, and the double
// occupies the second eightbyte. Must be coerced to {float, double}.
struct FloatDouble {
  float f;
  double d;
};
FloatDouble return_float_double() {
  return {1.0f, 2.0};
}
// LLVM: define dso_local { float, double } @_Z19return_float_doublev()
// OGCG: define dso_local { float, double } @_Z19return_float_doublev()

// Case 2: Single inheritance — base class contributes to register coercion.
// Derived has layout {int x (from Base), int y} = 8 bytes = 1 eightbyte.
// Must be coerced to i64, not {i32}.
struct Base {
  int x;
};
struct Derived : Base {
  int y;
};
Derived return_derived() {
  Derived d;
  d.x = 1;
  d.y = 2;
  return d;
}
// LLVM: define dso_local i64 @_Z14return_derivedv()
// OGCG: define dso_local i64 @_Z14return_derivedv()

// Case 3: Multi-inheritance — fields from multiple bases.
// C has layout {int a (from A), int b (from B), int c} = 12 bytes = 2
// eightbytes. Must be coerced to {i64, i32}.
struct A {
  int a;
};
struct B {
  int b;
};
struct C : A, B {
  int c;
};
C return_multi_inherit() {
  C c;
  c.a = 1;
  c.b = 2;
  c.c = 3;
  return c;
}
// LLVM: define dso_local { i64, i32 } @_Z20return_multi_inheritv()
// OGCG: define dso_local { i64, i32 } @_Z20return_multi_inheritv()

// Case 4: Virtual inheritance — class with virtual base is passed indirectly
// (in memory) because it has a non-trivial copy constructor. The classify
// function should never be called on the base class offsets because
// getRecordArgABI() returns RAA_Indirect before reaching that point.
struct VBase {
  int x;
};
struct VDerived : virtual VBase {
  int y;
};
void take_vderived(VDerived d);
void test_vderived() {
  VDerived d;
  d.x = 1;
  d.y = 2;
  take_vderived(d);
}
// VDerived is passed by pointer (indirect) because of virtual inheritance.
// LLVM: declare void @_Z13take_vderived8VDerived(ptr dead_on_return noundef)
// OGCG: declare void @_Z13take_vderived8VDerived(ptr dead_on_return noundef)
