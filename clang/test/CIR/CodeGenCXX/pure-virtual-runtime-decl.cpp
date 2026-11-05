// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s

struct A {
  A();
  virtual void f() = 0;
};
A::A() {}

struct B : A {
  B();
  void f() override {}
};
B::B() {}

int main() {
  B b;
  void (B::*pmf)() = &B::f;
  (b.*pmf)();
  return 0;
}

// CHECK: @_ZTV1A = linkonce_odr unnamed_addr constant
// CHECK-SAME: ptr @__cxa_pure_virtual
// CHECK: declare void @__cxa_pure_virtual() unnamed_addr
// CHECK-NOT: declare dso_local void @__cxa_pure_virtual()
