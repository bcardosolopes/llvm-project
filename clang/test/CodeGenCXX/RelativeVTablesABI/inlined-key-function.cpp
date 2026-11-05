// Inline comdat method definition example.
// The VTable is in a comdat and defined anywhere the inline definition is.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fclangir | FileCheck %s %}

// CHECK-DAG: $_ZTI1A = comdat any
// CHECK-DAG: $_ZTS1A = comdat any
// CHECK-DAG: $_ZTI1A.rtti_proxy = comdat any

// The VTable is linkonce_odr and in a comdat here bc its key function is inline defined.
// CHECK: @_ZTV1A.local = linkonce_odr hidden unnamed_addr constant { [3 x i32] }
// CHECK: @_ZTV1A = linkonce_odr unnamed_addr alias { [3 x i32] }, ptr @_ZTV1A.local

class A {
public:
  virtual void foo();
};
void A_foo(A *a);

inline void A::foo() {}

void func() {
  A a;
  A_foo(&a);
}
