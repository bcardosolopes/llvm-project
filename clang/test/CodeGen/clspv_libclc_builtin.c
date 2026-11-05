// RUN: %clang_cc1 -triple spir -emit-llvm %s -o - | FileCheck %s --check-prefix=CHECK
// RUN: %if cir-support %{ %clang_cc1 -triple spir -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CHECK %}
// RUN: %if cir-support %{ %clang_cc1 -triple spir -fclangir -emit-cir %s -o - | FileCheck %s --check-prefixes=FLAT %}

// CHECK: @foo()
// CHECK-SAME: !clspv_libclc_builtin
// FLAT: cir.func{{.*}} @foo()
// FLAT: cir.clspv_libclc_builtin

void __attribute__((clspv_libclc_builtin)) foo() {}
