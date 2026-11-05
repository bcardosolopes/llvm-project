// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Guard variable atomic load for static local initialization must use
// the guard variable's alignment (8 for i64), not the loaded type's natural
// alignment (1 for i8).

struct A { A(); };
void f() { static A a; }

struct B { B(); ~B(); };
void g() { static B b; }

// LLVM-LABEL: define {{.*}} @_Z1fv
// LLVM: load atomic i8, ptr @_ZGVZ1fvE1a acquire, align 8
// LLVM-LABEL: define {{.*}} @_Z1gv
// LLVM: load atomic i8, ptr @_ZGVZ1gvE1b acquire, align 8

// OGCG-LABEL: define {{.*}} @_Z1fv
// OGCG: load atomic i8, ptr @_ZGVZ1fvE1a acquire, align 8
// OGCG-LABEL: define {{.*}} @_Z1gv
// OGCG: load atomic i8, ptr @_ZGVZ1gvE1b acquire, align 8
