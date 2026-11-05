// RUN: %clang_cc1 -triple aarch64-unknown-fuchsia -std=c++17 -fclangir \
// RUN:   -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-fuchsia -std=c++17 \
// RUN:   -emit-llvm -o - %s | FileCheck %s

// Verify that the public vtable alias has unnamed_addr, matching OGCG.

struct A { virtual void f(); };
void A::f() {}

// CHECK: @_ZTV1A = unnamed_addr alias { [3 x i32] }, ptr @_ZTV1A.local
