// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck %s -input-file=%t.ll

// Pointer-to-member for field in nested anonymous struct/union.

struct S {
    union { struct { int x; }; };
};
int S::* p = &S::x;
// CHECK: @p = {{.*}}global i64 0

// Three levels
struct S2 {
    union { struct { union { int y; }; }; };
};
int S2::* p2 = &S2::y;
// CHECK: @p2 = {{.*}}global i64 0
