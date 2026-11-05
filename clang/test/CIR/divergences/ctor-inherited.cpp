// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Inherited constructor comdat generation.

// CIR: $_ZN7DerivedCI14BaseEi = comdat any
// CIR: define linkonce_odr void @_ZN7DerivedCI14BaseEi({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN7DerivedCI14BaseEi = comdat any
// OGCG: define linkonce_odr void @_ZN7DerivedCI14BaseEi({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct Base {
    int x;
    Base(int val) : x(val) {}
};

struct Derived : Base {
    using Base::Base;  // Inherit constructors
};

int test() {
    Derived d(42);
    return d.x;
}
