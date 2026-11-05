// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Diamond inheritance (non-virtual) comdat generation.

// CIR: $_ZN7DiamondC1Ev = comdat any
// CIR: define linkonce_odr void @_ZN7DiamondC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN7DiamondC1Ev = comdat any
// OGCG: define linkonce_odr void @_ZN7DiamondC1Ev({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct Base {
    int x = 1;
};

struct Left : Base {
    int y = 2;
};

struct Right : Base {
    int z = 3;
};

struct Diamond : Left, Right {
    int w = 4;
};

int test() {
    Diamond d;
    return d.Left::x + d.Right::x + d.y + d.z + d.w;
}
