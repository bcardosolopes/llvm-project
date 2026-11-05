// Cross comdat example
// Parent VTable is in a comdat section.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

// The inline function is emitted in each module with the same comdat
// CHECK-DAG: $_ZTI1A = comdat any
// CHECK-DAG: $_ZTS1A = comdat any
// CHECK-DAG: $_ZTI1B.rtti_proxy = comdat any
// CHECK-DAG: $_ZTI1A.rtti_proxy = comdat any

// The VTable for B is emitted here since it has a key function which is defined in this module
// CHECK: @_ZTV1B.local = internal unnamed_addr constant { [3 x i32] }

// The VTable for A is emitted here and in a comdat section since it has no key function, and is used in this module when creating an instance of A (in func()).
// CHECK: @_ZTV1A.local = linkonce_odr hidden unnamed_addr constant { [3 x i32] }

// CHECK: @_ZTV1B ={{.*}} unnamed_addr alias { [3 x i32] }, ptr @_ZTV1B.local
// CHECK: @_ZTV1A = linkonce_odr unnamed_addr alias { [3 x i32] }, ptr @_ZTV1A.local

// CHECK:      define{{.*}} void @_ZN1B3fooEv(ptr {{.*}}%{{[a-z0-9]+}}) unnamed_addr
// CHECK:        ret void
// CHECK-NEXT: }

class A {
public:
  inline virtual void foo() {}
};
class B : public A {
public:
  void foo() override;
};
void A_foo(A *a);

void B::foo() {}
void func2() {
  A a;
  A_foo(&a);
}
