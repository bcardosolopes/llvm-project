// Check the layout of the vtable for a child class that inherits a virtual
// function but does override it.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

// CHECK: @_ZTV1B.local = internal unnamed_addr constant { [4 x i32] }

// CHECK: @_ZTV1B ={{.*}} unnamed_addr alias { [4 x i32] }, ptr @_ZTV1B.local

class A {
public:
  virtual void foo();
};

class B : public A {
public:
  void foo() override;
  virtual void bar();
};

void A::foo() {}
void B::foo() {}

void A_foo(A *a) {
  a->foo();
}

void B_foo(B *b) {
  b->foo();
}
