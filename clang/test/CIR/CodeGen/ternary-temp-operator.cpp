// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that ternary with temporary objects and operator overloading works.
// Previously crashed.

struct A {
    A() : i(0) {}
    ~A() {}
    int i;
    friend const A& operator<<(const A& a, int n) { return a; }
};

void g(const A&) {}

void test(bool b) {
    g(b ? A() << 1 : A() << 2);
}

// CIR: cir.func {{.*}} @_Z4testb
// CIR:   cir.ternary
// CIR:     cir.call @_ZN1AC1Ev
// CIR:     cir.call @_ZlsRK1Ai
// CIR:     cir.yield
// CIR:   }, false {
// CIR:     cir.call @_ZN1AC1Ev
// CIR:     cir.call @_ZlsRK1Ai
// CIR:     cir.yield
// CIR:   cir.call @_Z1gRK1A
// CIR:   cir.if
// CIR:     cir.call @_ZN1AD1Ev
// CIR:   cir.if
// CIR:     cir.call @_ZN1AD1Ev

// LLVM: define {{.*}} @_Z4testb
// LLVM:   br i1
// LLVM:   call void @_ZN1AC1Ev
// LLVM:   call {{.*}} @_ZlsRK1Ai
// LLVM:   call void @_ZN1AC1Ev
// LLVM:   call {{.*}} @_ZlsRK1Ai
// LLVM:   call void @_Z1gRK1A
// LLVM:   call void @_ZN1AD1Ev
// LLVM:   call void @_ZN1AD1Ev
