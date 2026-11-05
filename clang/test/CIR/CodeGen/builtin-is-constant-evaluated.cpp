// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -std=c++20 -emit-cir -clangir-disable-passes -o %t.cir %s
// RUN: FileCheck %s -input-file=%t.cir

// __builtin_is_constant_evaluated() should fold to false at codegen.

bool test_direct() { return __builtin_is_constant_evaluated(); }
// CHECK: #cir.bool<false>
// CHECK-LABEL: @_Z11test_directv
// CHECK: cir.const #false
