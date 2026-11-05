// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// When a constructor call appears before the out-of-line
// constructor definition, and constructor aliases are enabled, the C1
// alias to C2 must be resolved correctly during CIR-to-LLVM lowering
// regardless of operation ordering in the module.

struct S {
    int x = 42;
    S();
};

void test() { S s; }

S::S() {}

// CIR: cir.func {{.*}}@_Z4testv()
// CIR:   cir.call @_ZN1SC1Ev(
// CIR-DAG: cir.func {{.*}}@_ZN1SC2Ev(
// CIR-DAG: cir.func {{.*}}@_ZN1SC1Ev({{.*}}) alias(@_ZN1SC2Ev)

// LLVM: @_ZN1SC1Ev = unnamed_addr alias void (ptr), ptr @_ZN1SC2Ev
// LLVM: define {{.*}}void @_Z4testv()
// LLVM:   call void @_ZN1SC1Ev(ptr
// LLVM: define {{.*}}void @_ZN1SC2Ev(ptr
