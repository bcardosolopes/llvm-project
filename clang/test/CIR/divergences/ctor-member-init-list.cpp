// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Constructor with member initializer list comdat generation.

// CIR: $_ZN5OuterC1Ev = comdat any
// CIR: define linkonce_odr void @_ZN5OuterC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN5OuterC1Ev = comdat any
// OGCG: define linkonce_odr void @_ZN5OuterC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct Inner {
    int val;
    Inner(int v) : val(v) {}
};

struct Outer {
    Inner i1, i2;
    Outer() : i1(10), i2(20) {}
};

int test() {
    Outer o;
    return o.i1.val + o.i2.val;
}
