// Check the vtable layout for classes with key functions defined in different
// translation units. This TU only manifests the vtable for A.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

#include "cross-tu-header.h"

// CHECK: $_ZTI1A.rtti_proxy = comdat any

// CHECK: @_ZTV1A.local = internal unnamed_addr constant { [4 x i32] }
// @_ZTV1A ={{.*}} unnamed_addr alias { [4 x i32] }, ptr @_ZTV1A.local

void A::foo() {}
void A_foo(A *a) { a->foo(); }
void A_bar(A *a) { a->bar(); }
