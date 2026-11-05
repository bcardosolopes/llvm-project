// Ensure that a COMDAT vtable always uses the .local alias, even when the
// vtable is dso_local (e.g. hidden visibility). This guarantees that all TUs
// produce structurally identical COMDAT groups regardless of visibility
// settings, preventing silent symbol loss at link time.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -o - -emit-llvm \
// RUN:   -fvisibility=hidden | FileCheck %s

// CHECK: @_ZTV1A.local = linkonce_odr hidden unnamed_addr constant { [3 x i32] }
// CHECK-SAME: comdat($_ZTV1A)
// CHECK: @_ZTV1A = linkonce_odr hidden unnamed_addr alias { [3 x i32] }, ptr @_ZTV1A.local

// Class with no key function => vtable is linkonce_odr in a COMDAT.
class A {
public:
  inline virtual void foo() {}
};

void func() { A a; }
