// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

struct A {
  char c;
  int A::*i;
};

// Simple virtual base with data member pointer.
struct B : virtual A {};
B b;

// LLVM: @b = global %struct.B { ptr null, %struct.A { i8 0, i64 -1 } }, align 8
// OGCG: @b = global %struct.B { ptr null, %struct.A { i8 0, i64 -1 } }, align 8

// Virtual base + own data member pointer in derived.
struct C : virtual A { int A::*i; };
C c;

// LLVM: @c = global %struct.C { ptr null, i64 -1, %struct.A { i8 0, i64 -1 } }, align 8
// OGCG: @c = global %struct.C { ptr null, i64 -1, %struct.A { i8 0, i64 -1 } }, align 8

// Derived from C with own data member pointer — multi-level virtual base.
struct D : C { int A::*i; };
D d;

// LLVM: @d = global %struct.D { %struct.C.base { ptr null, i64 -1 }, i64 -1, %struct.A { i8 0, i64 -1 } }, align 8
// OGCG: @d = global %struct.D { %struct.C.base { ptr null, i64 -1 }, i64 -1, %struct.A { i8 0, i64 -1 } }, align 8
