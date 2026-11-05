// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Copy constructor comdat generation.

// CIR: $_ZN1SC1ERKS_ = comdat any
// CIR: define linkonce_odr void @_ZN1SC1ERKS_({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN1SC1ERKS_ = comdat any
// OGCG: define linkonce_odr void @_ZN1SC1ERKS_({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct S {
    int x;
    S(int val) : x(val) {}
    S(const S& other) : x(other.x * 2) {}
};

int test() {
    S s1(10);
    S s2(s1);
    return s2.x;
}
