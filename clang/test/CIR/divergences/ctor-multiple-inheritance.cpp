// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Multiple inheritance constructors comdat generation.

// CIR: $_ZN1CC1Ev = comdat any
// CIR: define linkonce_odr void @_ZN1CC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN1CC1Ev = comdat any
// OGCG: define linkonce_odr void @_ZN1CC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct A {
    int a = 1;
};

struct B {
    int b = 2;
};

struct C : A, B {
    int c = 3;
};

int test() {
    C obj;
    return obj.a + obj.b + obj.c;
}
