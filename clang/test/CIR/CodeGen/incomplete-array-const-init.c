// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Test: array with size deduced from initializer (originally incomplete array).
static int arr[] = {10, 20, 30};
int *get_arr(void) { return arr; }

// LLVM: @arr = internal global [3 x i32] [i32 10, i32 20, i32 30]
// OGCG: @arr = internal global [3 x i32] [i32 10, i32 20, i32 30]

// Flexible array member initialization (GNU extension).
struct S {
  int len;
  int data[];
};

static struct S s = {3, {1, 2, 3}};
int *get_data(void) { return s.data; }

// LLVM: @s = internal global { i32, [3 x i32] } { i32 3, [3 x i32] [i32 1, i32 2, i32 3] }
// OGCG: @s = internal global { i32, [3 x i32] } { i32 3, [3 x i32] [i32 1, i32 2, i32 3] }
