// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test that a [[noreturn]] destructor in various statement and expression
// contexts generates valid CIR. Related to ternary-noreturn-dtor.cpp which
// covers the ternary case; this covers if/while/for/lambda and the
// ternary-with-comma-expression variant.

struct S { [[noreturn]] ~S(); };

void test_if(bool c) { if (!c) { S(); } }

// CIR-LABEL: cir.func{{.*}} @_Z7test_ifb(
// CIR:         cir.if
// CIR:           cir.scope {
// CIR:             cir.call @_ZN1SD1Ev({{.*}}) nothrow
// CIR:             cir.unreachable
// CIR:           }

// LLVM-LABEL: define{{.*}} void @_Z7test_ifb(
// LLVM:         call void @_ZN1SD1Ev(
// LLVM:         unreachable

// OGCG-LABEL: define{{.*}} void @_Z7test_ifb(
// OGCG:         call void @_ZN1SD1Ev(
// OGCG:         unreachable

void test_while() { while(true) { S(); } }

// CIR-LABEL: cir.func{{.*}} @_Z10test_whilev(
// CIR:         cir.while
// CIR:           cir.scope {
// CIR:             cir.call @_ZN1SD1Ev({{.*}}) nothrow
// CIR:             cir.unreachable
// CIR:           }

// LLVM-LABEL: define{{.*}} void @_Z10test_whilev(
// LLVM:         call void @_ZN1SD1Ev(
// LLVM:         unreachable

// OGCG-LABEL: define{{.*}} void @_Z10test_whilev(
// OGCG:         call void @_ZN1SD1Ev(
// OGCG:         unreachable

void test_for(int n) { for (int i = 0; i < n; i++) { S(); } }

// CIR-LABEL: cir.func{{.*}} @_Z8test_fori(
// CIR:         cir.for
// CIR:           cir.scope {
// CIR:             cir.call @_ZN1SD1Ev({{.*}}) nothrow
// CIR:             cir.unreachable
// CIR:           }

// LLVM-LABEL: define{{.*}} void @_Z8test_fori(
// LLVM:         call void @_ZN1SD1Ev(
// LLVM:         unreachable

// OGCG-LABEL: define{{.*}} void @_Z8test_fori(
// OGCG:         call void @_ZN1SD1Ev(
// OGCG:         unreachable

void test_lambda() { auto l = []() { S(); }; l(); }

// CIR-LABEL: cir.func{{.*}} @_Z11test_lambdav(

// LLVM-LABEL: define{{.*}} void @_Z11test_lambdav(

// OGCG-LABEL: define{{.*}} void @_Z11test_lambdav(

bool test_ternary_comma(bool c) { return c ? true : (S(), false); }

// CIR-LABEL: cir.func{{.*}} @_Z18test_ternary_commab(
// CIR:         cir.ternary({{.*}}, true {
// CIR:           cir.yield
// CIR:         }, false {
// CIR:           cir.call @_ZN1SD1Ev({{.*}}) nothrow
// CIR:           cir.unreachable

// LLVM-LABEL: define{{.*}} @_Z18test_ternary_commab(
// LLVM:         call void @_ZN1SD1Ev(
// LLVM:         unreachable

// OGCG-LABEL: define{{.*}} @_Z18test_ternary_commab(
// OGCG:         call void @_ZN1SD1Ev(
// OGCG:         unreachable
