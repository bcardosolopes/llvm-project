// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Verify that constant initializers for structs containing vector members
// compute correct element offsets using alloc size (not store size).
// For <3 x i32>, store size = 12 but alloc size = 16.

typedef int v3i __attribute__((ext_vector_type(3)));

struct S {
  v3i a;    // store=12, alloc=16
  int b;    // at offset 16, not 12
};

struct S gs = {{1, 2, 3}, 42};

// LLVM: @gs = global %struct.S { <3 x i32> <i32 1, i32 2, i32 3>, i32 42 }, align 16
// OGCG: @gs = global %struct.S { <3 x i32> <i32 1, i32 2, i32 3>, i32 42 }, align 16

// Array of vectors: elements spaced by alloc size.
v3i ga[2] = {{1, 2, 3}, {4, 5, 6}};

// LLVM: @ga = global [2 x <3 x i32>] [<3 x i32> <i32 1, i32 2, i32 3>, <3 x i32> <i32 4, i32 5, i32 6>], align 16
// OGCG: @ga = global [2 x <3 x i32>] [<3 x i32> <i32 1, i32 2, i32 3>, <3 x i32> <i32 4, i32 5, i32 6>], align 16

struct T {
  v3i vec;
  int x;
  int y;
};

struct T gt = {{10, 20, 30}, 100, 200};

// LLVM: @gt = global %struct.T { <3 x i32> <i32 10, i32 20, i32 30>, i32 100, i32 200 }, align 16
// OGCG: @gt = global %struct.T { <3 x i32> <i32 10, i32 20, i32 30>, i32 100, i32 200 }, align 16
