// Check the vtable layout for classes with key functions defined in different
// translation units. This TU manifests the vtable for B.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

#include "cross-tu-header.h"

// CHECK: $_ZTI1B.rtti_proxy = comdat any

// CHECK: @_ZTV1B.local = internal unnamed_addr constant { [4 x i32] }
// CHECK: @_ZTV1B ={{.*}} unnamed_addr alias { [4 x i32] }, ptr @_ZTV1B.local

// A::bar() is defined outside of the module that defines the vtable for A
// CHECK:      define{{.*}} void @_ZN1A3barEv(ptr {{.*}}%{{[a-z0-9]+}}) unnamed_addr
// CHECK:        ret void
// CHECK-NEXT: }

// CHECK:      define{{.*}} void @_ZN1B3fooEv(ptr {{.*}}%{{[a-z0-9]+}}) unnamed_addr
// CHECK:        ret void
// CHECK-NEXT: }

void A::bar() {}
void B::foo() {}
