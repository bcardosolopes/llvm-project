// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// C++17 inline static data member. Verify that CIR emits the global with
// linkonce_odr linkage and a comdat group, matching OGCG behavior.

struct S {
    inline static int value = 42;
};

int test() {
    return S::value;
}

// CIR: cir.global linkonce_odr{{.*}} comdat{{.*}} @_ZN1S5valueE = #cir.int<42>

// LLVM: @_ZN1S5valueE = linkonce_odr global i32 42, comdat
// LLVM: define{{.*}} @_Z4testv()

// OGCG: @_ZN1S5valueE = linkonce_odr global i32 42, comdat
// OGCG: define{{.*}} @_Z4testv()
