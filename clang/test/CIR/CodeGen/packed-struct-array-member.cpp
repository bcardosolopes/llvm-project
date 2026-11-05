// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that global struct initializers with array members that have trailing
// zeros use packed struct optimization (matching OGCG's EmitArrayConstant).
// Arrays with >= 8 trailing zeros are emitted as:
//   <{ nonzero_elts..., [trailingZeros x T] zeroinitializer }>

struct Inner {
  void *p, *q;
};

struct S {
  char a;
  char b;
  char data[34];
  char c;
  Inner inner;
  int d;
};

// Array member with 3 nonzero + 31 trailing zeros inside a struct.
// CHECK: @g1 = global { i8, i8, <{ i8, i8, i8, [31 x i8] }>, i8, %struct.Inner, i32 }
// CHECK-SAME: <{ i8 10, i8 20, i8 30, [31 x i8] zeroinitializer }>
S g1 = {1, 2, {10, 20, 30}, 3, {nullptr, nullptr}, 4};

// Struct with multiple array members, each partially initialized.
struct Multi {
  char a[32];
  char b[32];
};

// CHECK: @g2 = global { <{ i8, i8, [30 x i8] }>, <{ i8, i8, [30 x i8] }> }
// CHECK-SAME: <{ i8 1, i8 2, [30 x i8] zeroinitializer }>
// CHECK-SAME: <{ i8 3, i8 4, [30 x i8] zeroinitializer }>
Multi g2 = {{1, 2}, {3, 4}};

// Nested struct containing a partially-initialized array.
struct Wrap {
  int id;
  Multi inner;
  int end;
};

// CHECK: @g3 = global { i32, { <{ i8, [31 x i8] }>, <{ i8, [31 x i8] }> }, i32 }
// CHECK-SAME: <{ i8 1, [31 x i8] zeroinitializer }>
// CHECK-SAME: <{ i8 2, [31 x i8] zeroinitializer }>
// CHECK-SAME: i32 99
Wrap g3 = {42, {{1}, {2}}, 99};

// Packed trailing-zero bool arrays must use the memory element type (i8),
// matching normal array lowering.
// CHECK: @g4 = global <{ i8, [8 x i8] }> <{ i8 1, [8 x i8] zeroinitializer }>, align 1
bool g4[9] = {true};
