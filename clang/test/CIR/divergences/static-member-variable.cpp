// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Static member variables are emitted as global definitions with the
// mangled name. Both CIR and OGCG produce identical global declarations.

struct S {
    static int counter;
};

int S::counter = 100;

int test() {
    return S::counter;
}

// CIR: @_ZN1S7counterE = global i32 100, align 4
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: @_ZN1S7counterE = global i32 100, align 4
// OGCG: define dso_local noundef i32 @_Z4testv()
