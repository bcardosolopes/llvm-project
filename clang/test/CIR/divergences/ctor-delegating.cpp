// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Delegating constructor comdat generation.

// CIR: $_ZN1SC1Eii = comdat any
// CIR: $_ZN1SC1Ei = comdat any
// CIR: define linkonce_odr void @_ZN1SC1Eii({{.*}}) unnamed_addr #{{[0-9]+}} comdat
// CIR: define linkonce_odr void @_ZN1SC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN1SC1Ei = comdat any
// OGCG: $_ZN1SC1Eii = comdat any
// OGCG: define linkonce_odr void @_ZN1SC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat
// OGCG: define linkonce_odr void @_ZN1SC1Eii({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct S {
    int x, y;
    S(int a) : S(a, a * 2) {}  // Delegating
    S(int a, int b) : x(a), y(b) {}
};

int test() {
    S s(5);
    return s.x + s.y;
}
