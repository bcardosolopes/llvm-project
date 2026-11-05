// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Deep inheritance chain constructors comdat generation.

// CIR: $_ZN5ChildC1Ev = comdat any
// CIR: define linkonce_odr void @_ZN5ChildC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN5ChildC1Ev = comdat any
// OGCG: define linkonce_odr void @_ZN5ChildC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct GrandParent {
    int a = 1;
};

struct Parent : GrandParent {
    int b = 2;
};

struct Child : Parent {
    int c = 3;
};

int test() {
    Child ch;
    return ch.a + ch.b + ch.c;
}
