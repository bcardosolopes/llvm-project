// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that goto backward past RAII objects produces valid CIR with proper
// cleanup and return (not cir.yield at function scope).

struct X { ~X(); };

// CHECK-LABEL: @_Z3foov
void foo() {
    X v;
TryAgain:
    goto TryAgain;
}
// CHECK: cir.label "TryAgain"
// CHECK: cir.goto "TryAgain"
// CHECK-NOT: cir.call @_ZN1XD1Ev
// CHECK: } loc(

// Multiple RAII objects: B destroyed before A.
void doesntThrow() throw();
struct F { ~F() { doesntThrow(); } };

// CHECK-LABEL: @_Z5atestv
void atest() {
    F A;
lab:
    F B;
    goto lab;
}
// CHECK: cir.label "lab"
// CHECK: cir.call @_ZN1FD1Ev
// CHECK: cir.goto "lab"
// CHECK: } loc(
