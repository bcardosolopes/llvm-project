// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Verify that member function pointer globals with CK_ReinterpretMemberPointer
// casts are constant-initialized (no runtime __cxx_global_var_init).

namespace PR5138 {
  struct foo { virtual void bar(foo *); };
  void (foo::*ptr1)(void *) = (void (foo::*)(void *))&foo::bar;
  void (foo::*ptr3)(void) = (void (foo::*)(void))&foo::bar;
}

// Virtual member function pointers: constant init with {1, 0}.
// CIR: cir.global external @_ZN6PR51384ptr1E = #cir.const_record<{#cir.int<1> : !s64i, #cir.int<0> : !s64i}>
// CIR: cir.global external @_ZN6PR51384ptr3E = #cir.const_record<{#cir.int<1> : !s64i, #cir.int<0> : !s64i}>
// CIR-NOT: __cxx_global_var_init

// LLVM: @_ZN6PR51384ptr1E = global { i64, i64 } { i64 1, i64 0 }, align 8
// LLVM: @_ZN6PR51384ptr3E = global { i64, i64 } { i64 1, i64 0 }, align 8
// LLVM-NOT: __cxx_global_var_init

// OGCG: @_ZN6PR51384ptr1E = global { i64, i64 } { i64 1, i64 0 }, align 8
// OGCG: @_ZN6PR51384ptr3E = global { i64, i64 } { i64 1, i64 0 }, align 8

// Non-virtual case.
struct S { void method(int); };
void (S::*ptr_nv)(long) = (void (S::*)(long))&S::method;

// LLVM: @ptr_nv = global { i64, i64 } { i64 ptrtoint (ptr @_ZN1S6methodEi to i64), i64 0 }, align 8
// LLVM-NOT: define internal void @__cxx_global_var_init

// OGCG: @ptr_nv = global { i64, i64 } { i64 ptrtoint (ptr @_ZN1S6methodEi to i64), i64 0 }, align 8

// Null member function pointer cast through derived-to-base: null stays null.
struct A { void foo(A *); };
struct B { int b; };
struct C : B, A { int c; };
void (C::*ptr_null)(void *) = (void (C::*)(void *)) (void (A::*)(void *)) (void (A::*)(A *)) 0;

// CIR: cir.global external @ptr_null = #cir.const_record<{#cir.int<0> : !s64i, #cir.int<0> : !s64i}>
// LLVM: @ptr_null = global { i64, i64 } zeroinitializer, align 8
// OGCG: @ptr_null = global { i64, i64 } zeroinitializer, align 8
