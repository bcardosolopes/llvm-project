// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

struct A { int a; int b; };

// Null data member pointer array — null representation is -1 (Itanium ABI).
int A::* arr[2];
// LLVM: @arr = global [2 x i64] [i64 -1, i64 -1]
// OGCG: @arr = global [2 x i64] [i64 -1, i64 -1]

// Nested array of data member pointers.
int A::* arr2d[2][2];
// LLVM: @arr2d = global [2 x [2 x i64]] {{\[}}[2 x i64] [i64 -1, i64 -1], [2 x i64] [i64 -1, i64 -1]]
// OGCG: @arr2d = global [2 x [2 x i64]] {{\[}}[2 x i64] [i64 -1, i64 -1], [2 x i64] [i64 -1, i64 -1]]

// Struct containing data member pointer array.
struct S {
  int A::*members[3];
};
S s;
void test() { (void)s; }
// LLVM: @s = global %struct.S { [3 x i64] [i64 -1, i64 -1, i64 -1] }
// OGCG: @s = global %struct.S { [3 x i64] [i64 -1, i64 -1, i64 -1] }
