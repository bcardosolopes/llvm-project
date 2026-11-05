// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Bug 1: TLS guard variables should use i8 (not i64), because
// thread-local storage is inherently thread-safe per-thread and doesn't
// need __cxa_guard_acquire (which requires an i64 guard).

int g();

int f() {
  thread_local int n = g();
  return n;
}

// Non-TLS static local should still use i64 guard with __cxa_guard_acquire.
struct A { A(); };
void h() { static A a; }

// Guard variable for TLS local static should be i8, align 1.
// LLVM-DAG: @_ZGVZ1fvE1n = internal thread_local global i8 0, align 1
// OGCG-DAG: @_ZGVZ1fvE1n = internal thread_local global i8 0, align 1

// Non-TLS static local should still use i64 guard.
// LLVM-DAG: @_ZGVZ1hvE1a = internal global i64 0, align 8
// OGCG-DAG: @_ZGVZ1hvE1a = internal global i64 0, align 8
