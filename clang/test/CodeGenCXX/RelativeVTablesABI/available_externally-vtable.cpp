// Check that available_externally vtables do not receive aliases.
// We check this specifically under the legacy pass manager because the new pass
// manager seems to remove available_externally vtables from the IR entirely.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -disable-llvm-passes -o - -emit-llvm | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -disable-llvm-passes -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=CIR %}

// The VTable for A is available_externally, meaning it can have a definition in
// IR, but is never emitted in this compilation unit. Because it won't be
// emitted here, we cannot make an alias, but we won't need to in the first
// place.
// CHECK: @_ZTV1A = available_externally unnamed_addr constant { [3 x i32] }
// CHECK-NOT: @_ZTV1A = {{.*}}alias
// CIR: @_ZTV1A = available_externally unnamed_addr constant { [3 x i32] }
// CIR-SAME: ptr dso_local_equivalent @_ZN1A3fooEv
// CIR: @_ZTI1A.rtti_proxy = linkonce_odr hidden unnamed_addr constant ptr @_ZTI1A, comdat
// CIR-NOT: @_ZTV1A = {{.*}}alias

class A {
public:
  virtual void foo();
};
void A_foo(A *a);

void func() {
  A a;
  A_foo(&a);
}
