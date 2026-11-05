// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that member-pointer data access uses inbounds GEP, matching OGCG.

struct A { int x; int y; };

// Normal access: base pointer is NOT constant null -> inbounds
int test_deref(A* a, int A::*mp) { return a->*mp; }

// CIR-LABEL: @_Z10test_derefP1AMS_i
// CIR: cir.ptr_stride inbounds

// LLVM-LABEL: @_Z10test_derefP1AMS_i
// LLVM: getelementptr inbounds i8

// OGCG-LABEL: @_Z10test_derefP1AMS_i
// OGCG: getelementptr inbounds i8

// Dot-star with lvalue: inbounds
int test_dotstar(A& a, int A::*mp) { return a.*mp; }

// CIR-LABEL: @_Z12test_dotstarR1AMS_i
// CIR: cir.ptr_stride inbounds

// LLVM-LABEL: @_Z12test_dotstarR1AMS_i
// LLVM: getelementptr inbounds i8

// OGCG-LABEL: @_Z12test_dotstarR1AMS_i
// OGCG: getelementptr inbounds i8
