// Cross comdat example
// Both the parent and child VTablea are in their own comdat sections.

// RUN: %clang_cc1 %s -triple=aarch64 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables | FileCheck %s
// RUN: %clang_cc1 %s -triple=x86_64 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables -fclangir | FileCheck %s %}

// Comdats are emitted for both A and B in this module and for their respective implementations of foo().
// CHECK-DAG: $_ZN1A3fooEv = comdat any
// CHECK-DAG: $_ZN1B3fooEv = comdat any
// CHECK-DAG: $_ZTI1A = comdat any
// CHECK-DAG: $_ZTS1A = comdat any
// CHECK-DAG: $_ZTI1A.rtti_proxy = comdat any
// CHECK-DAG: $_ZTI1B = comdat any
// CHECK-DAG: $_ZTS1B = comdat any
// CHECK-DAG: $_ZTI1B.rtti_proxy = comdat any

// Both the vtables for A and B are emitted and in their own comdats.
// CHECK-DAG: @_ZTV1A.local = linkonce_odr hidden unnamed_addr constant { [3 x i32] }
// CHECK-DAG: @_ZTV1B.local = linkonce_odr hidden unnamed_addr constant { [3 x i32] }

// CHECK-DAG: @_ZTV1A = linkonce_odr unnamed_addr alias { [3 x i32] }, ptr @_ZTV1A.local
// CHECK-DAG: @_ZTV1B = linkonce_odr unnamed_addr alias { [3 x i32] }, ptr @_ZTV1B.local

// The stubs and implementations for foo() are in their own comdat sections.
// CHECK:      define linkonce_odr void @_ZN1A3fooEv(ptr {{.*}}%{{[a-z0-9]+}}) unnamed_addr #{{[0-9]+}} comdat

// CHECK:      define linkonce_odr void @_ZN1B3fooEv(ptr {{.*}}%{{[a-z0-9]+}}) unnamed_addr #{{[0-9]+}} comdat

class A {
public:
  inline virtual void foo() {}
};
class B : public A {
public:
  inline void foo() override {}
};
void A_foo(A *a);

// func() is used so that the vtable for B is accessed when creating the instance.
void func() {
  A a;
  B b;
  A_foo(&a);
  A_foo(&b);
}
