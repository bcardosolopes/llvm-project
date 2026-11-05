// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Empty base optimization — comdat generation.

// CIR: $_ZN7DerivedC1Ev = comdat any
// CIR: define linkonce_odr void @_ZN7DerivedC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN7DerivedC1Ev = comdat any
// OGCG: define linkonce_odr void @_ZN7DerivedC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct Empty {};

struct Derived : Empty {
    int x = 42;
};

int test() {
    Derived d;
    return d.x;
}
