// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++11 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++11 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Regression test: CIR must implement conditional cleanup
// guards for temporaries in ternary expressions. Only the actually-constructed
// temporary should be destroyed.

struct B { B(); ~B(); };
struct C { C(); ~C(); };
struct A { A(const B&); A(const C&); };
void g(A);

void f(bool b) {
  // B() and C() are different types — ternary requires explicit conversion.
  g(b ? A(B()) : A(C()));
}

// CIR-LABEL: @_Z1fb
// Cleanup guard allocas must be at function scope (outside ternary regions).
// CIR: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]

// True branch: construct B, guard it, construct A from B.
// CIR: cir.call @_ZN1BC1Ev
// CIR: cir.call @_ZN1AC1ERK1B

// False branch: construct C, guard it, construct A from C.
// CIR: cir.call @_ZN1CC1Ev
// CIR: cir.call @_ZN1AC1ERK1C

// After the branch, call g, then destroy only the guarded branch temporaries.
// CIR: cir.call @_Z1g1A
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @_ZN1CD1Ev
// CIR: } loc(
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @_ZN1BD1Ev
// CIR: } loc(

// LLVM-LABEL: define {{.*}} @_Z1fb
// LLVM: store i8 0, ptr %{{.*}}
// LLVM: store i8 0, ptr %{{.*}}
// After the call, the cleanup guards destroy C first, then B.
// LLVM: call void @_Z1g1A
// LLVM: call void @_ZN1CD1Ev
// LLVM: call void @_ZN1BD1Ev

// OGCG-LABEL: define {{.*}} @_Z1fb
// OGCG: %cleanup.cond = alloca i1
// OGCG: %cleanup.cond2 = alloca i1
// OGCG: store i1 false, ptr %cleanup.cond
// OGCG: store i1 false, ptr %cleanup.cond2
// Cleanup order is LIFO: C destroyed first, then B.
// OGCG: call void @_ZN1CD1Ev
// OGCG: call void @_ZN1BD1Ev
