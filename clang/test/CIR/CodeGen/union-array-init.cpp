// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that union arrays with heterogeneous member initializers emit
// all elements, including trailing zero-initialized ones.
// Fixes: previously only explicit elements were emitted.
// Verify trailing zeros are present.

namespace PR37560 {
union U {
  char x;
  int a;
};
// 3 explicit + 13 zero-initialized = 16 elements total (64 bytes)
U d[16] = {'a', {.a = 123}, 'b'};
}

// Verify all 16 elements in the packed struct type and initializer:
// CHECK: @_ZN7PR375601dE = global <{
// 3 explicit elements with different types:
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i32 }
// CHECK-SAME: { i8, [3 x i8] }
// 13 trailing zero-initialized elements:
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: { i8, [3 x i8] }
// CHECK-SAME: }> <{
// Explicit initializer values:
// CHECK-SAME: i8 97
// CHECK-SAME: i32 123
// CHECK-SAME: i8 98
// Trailing zeros:
// CHECK-SAME: undef
// CHECK-SAME: }>, align 16
