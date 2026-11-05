// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test that a [[noreturn]] function call in a loop condition expression
// generates valid CIR. The noreturn call handling creates an UnreachableOp
// + new block, which must be cleaned up to maintain the SizedRegion<1>
// constraint on loop condition regions.

[[noreturn]] bool noreturn_func();

// Test: while loop with noreturn condition
void test_while() {
    while (noreturn_func()) {
    }
}

// CIR-LABEL: cir.func{{.*}} @_Z10test_whilev(
// CIR:         cir.while {
// CIR:           cir.call @_Z13noreturn_funcv()
// CIR:           cir.condition
// CIR:         } do {

// LLVM-LABEL: define{{.*}} void @_Z10test_whilev(
// LLVM:         call {{.*}} @_Z13noreturn_funcv()
// LLVM:         br

// OGCG-LABEL: define{{.*}} void @_Z10test_whilev(
// OGCG:         call {{.*}} @_Z13noreturn_funcv()
// OGCG:         unreachable

// Test: for loop with noreturn condition
void test_for() {
    for (int i = 0; noreturn_func(); i++) {
    }
}

// CIR-LABEL: cir.func{{.*}} @_Z8test_forv(
// CIR:         cir.for :
// CIR:           cir.call @_Z13noreturn_funcv()
// CIR:           cir.condition
// CIR:         } step {

// LLVM-LABEL: define{{.*}} void @_Z8test_forv(
// LLVM:         call {{.*}} @_Z13noreturn_funcv()
// LLVM:         br

// OGCG-LABEL: define{{.*}} void @_Z8test_forv(
// OGCG:         call {{.*}} @_Z13noreturn_funcv()
// OGCG:         unreachable

// Test: do-while loop with noreturn condition
void test_do_while() {
    do {
    } while (noreturn_func());
}

// CIR-LABEL: cir.func{{.*}} @_Z13test_do_whilev(
// CIR:         cir.do {
// CIR:         } while {
// CIR:           cir.call @_Z13noreturn_funcv()
// CIR:           cir.condition

// LLVM-LABEL: define{{.*}} void @_Z13test_do_whilev(
// LLVM:         call {{.*}} @_Z13noreturn_funcv()
// LLVM:         br

// OGCG-LABEL: define{{.*}} void @_Z13test_do_whilev(
// OGCG:         call {{.*}} @_Z13noreturn_funcv()
// OGCG:         unreachable

// Test: noreturn method call (matches the TProtocol.h pattern)
struct Protocol {
    [[noreturn]] bool peekMap();
};

void test_method(Protocol& p) {
    while (p.peekMap()) {
        int x = 1;
    }
}

// CIR-LABEL: cir.func{{.*}} @_Z11test_methodR8Protocol(
// CIR:         cir.while {
// CIR:           cir.call @_ZN8Protocol7peekMapEv(
// CIR:           cir.condition
// CIR:         } do {

// LLVM-LABEL: define{{.*}} void @_Z11test_methodR8Protocol(
// LLVM:         call {{.*}} @_ZN8Protocol7peekMapEv(
// LLVM:         br

// OGCG-LABEL: define{{.*}} void @_Z11test_methodR8Protocol(
// OGCG:         call {{.*}} @_ZN8Protocol7peekMapEv(
// OGCG:         unreachable
