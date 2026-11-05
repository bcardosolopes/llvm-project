// Diamond inheritance.
// A more complicated multiple inheritance example that includes longer chain of inheritance and a common ancestor.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

// VTable for B should contain offset to top (0), RTTI pointer, A::foo(), and B::barB().
// CHECK: @_ZTV1B.local = internal unnamed_addr constant { [4 x i32] }

// VTable for C should contain offset to top (0), RTTI pointer, A::foo(), and C::barC().
// CHECK: @_ZTV1C.local = internal unnamed_addr constant { [4 x i32] }

// VTable for D should be similar to the mutiple inheritance example where this
// vtable contains 2 inner vtables:
// - 1st table containing D::foo(), B::barB(), and D::baz().
// - 2nd table containing a thunk to D::foo() and C::barC().
// CHECK: @_ZTV1D.local = internal unnamed_addr constant { [5 x i32], [4 x i32] }

// @_ZTV1B ={{.*}} unnamed_addr alias { [4 x i32] }, ptr @_ZTV1B.local
// @_ZTV1C ={{.*}} unnamed_addr alias { [4 x i32] }, ptr @_ZTV1C.local
// @_ZTV1D ={{.*}} unnamed_addr alias { [5 x i32], [4 x i32] }, ptr @_ZTV1D.local

class A {
public:
  virtual void foo();
};

class B : public A {
public:
  virtual void barB();
};

class C : public A {
  virtual void barC();
};

// Should be a struct with 2 arrays from 2 parents.
// The 1st contains D::foo(), B::barB(), and D::baz().
// The 2nd contains C::barC(), and a thunk that points to D::foo().
class D : public B, C {
public:
  virtual void baz();
  void foo() override;
};

void B::barB() {}
void C::barC() {}
void D::foo() {}
void D::baz() {}

void D_foo(D *d) {
  d->foo();
}
