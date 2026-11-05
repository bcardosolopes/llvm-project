// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Member function pointer cast chains should be constant-folded
// at compile time instead of emitting dynamic initializers.

struct A { virtual void foo(); };
struct B : A {};
struct C : B {};
struct D : C {};

// OGCG constant-folds the chain of member pointer casts.
// CIR should produce the same constant initializer (no global_ctors entry).
void (D::*d)() = (void (C::*)()) (void (B::*)()) &A::foo;

// CHECK: @d = global { i64, i64 } { i64 1, i64 0 }, align 8
// CHECK-NOT: @llvm.global_ctors

// OGCG: @d = global { i64, i64 } { i64 1, i64 0 }, align 8
// OGCG-NOT: @llvm.global_ctors
