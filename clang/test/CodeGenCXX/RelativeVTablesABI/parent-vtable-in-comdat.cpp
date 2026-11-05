// Cross comdat example
// Parent VTable is in a comdat section.

// RUN: %clang_cc1 %s -triple=aarch64 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables | FileCheck %s
// RUN: %clang_cc1 %s -triple=x86_64 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables -fclangir | FileCheck %s %}

// A::foo() has a comdat since it is an inline function
// CHECK-DAG: $_ZN1A3fooEv = comdat any
// CHECK-DAG: $_ZTI1A = comdat any
// CHECK-DAG: $_ZTS1A = comdat any

// CHECK-DAG: $_ZTI1A.rtti_proxy = comdat any

// The VTable for A is emitted here and in a comdat section since it has no key function, and is used in this module when creating an instance of A.
// CHECK: @_ZTV1A.local = linkonce_odr hidden unnamed_addr constant { [3 x i32] }
// CHECK: @_ZTS1A = linkonce_odr constant [3 x i8] c"1A\00", comdat, align 1
// CHECK: @_ZTI1A.rtti_proxy = linkonce_odr hidden unnamed_addr constant ptr @_ZTI1A, comdat
// CHECK: @_ZTV1A = linkonce_odr unnamed_addr alias { [3 x i32] }, ptr @_ZTV1A.local

// CHECK:      define linkonce_odr void @_ZN1A3fooEv(ptr {{.*}}) unnamed_addr #{{[0-9]+}} comdat

class A {
public:
  inline virtual void foo() {}
};
class B : public A {
public:
  void foo() override;
};
void A_foo(A *a);

void A_foo(A *a) { a->foo(); }

// func() is only used to emit a vtable.
void func() {
  A a;
  A_foo(&a);
}
