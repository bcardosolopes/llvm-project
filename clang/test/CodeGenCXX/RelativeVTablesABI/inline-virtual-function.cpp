// The VTable is not in a comdat but the inline methods are.
// This doesn’t affect the vtable or the stubs we emit.

// RUN: %clang_cc1 %s -triple=aarch64 -O1 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %clang_cc1 %s -triple=x86_64 -O1 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64 -O1 -o - -emit-llvm -fexperimental-relative-c++-abi-vtables -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

// CHECK: $_ZTI1A.rtti_proxy = comdat any

// The vtable has a key function (A::foo()) so it does not have a comdat
// CHECK: @_ZTV1A.local = internal unnamed_addr constant { [4 x i32] }
// CHECK: @_ZTV1A ={{.*}} unnamed_addr alias { [4 x i32] }, ptr @_ZTV1A.local

class A {
public:
  virtual void foo(); // Key func
  inline virtual void bar() {}
};

void A::foo() {}
