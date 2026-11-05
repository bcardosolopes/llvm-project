// RUN: %clang_cc1 -triple x86_64-apple-darwin -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s

namespace test2 {
struct A {
  A();
  A(const A &);
  ~A();
};

struct B {
  B();
  B(const B &);
  ~B();
};

void test() {
  __block A a;
  __block B b;
  ^{ (void)a; (void)b; };
}

// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: call void @_ZN5test21AC1ERKS0_(
// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: call void @_ZN5test21AD1Ev(
// CHECK-LABEL: define internal void @__Block_byref_object_copy_.1(
// CHECK: call void @_ZN5test21BC1ERKS0_(
// CHECK-LABEL: define internal void @__Block_byref_object_dispose_.1(
// CHECK: call void @_ZN5test21BD1Ev(
} // namespace test2
