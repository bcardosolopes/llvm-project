// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Template inheritance comdat generation.

// CIR: $_ZN7DerivedC1Ei = comdat any
// CIR: define linkonce_odr void @_ZN7DerivedC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN7DerivedC1Ei = comdat any
// OGCG: define linkonce_odr void @_ZN7DerivedC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat

template<typename T>
struct Base {
    T value;
    Base(T v) : value(v) {}
};

struct Derived : Base<int> {
    Derived(int v) : Base<int>(v) {}
};

int test() {
    Derived d(42);
    return d.value;
}
