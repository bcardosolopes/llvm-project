// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Template with multiple type parameters. The instantiation is emitted with
// comdat linkage (linkonce_odr) in both CIR and OGCG.

template<typename T, typename U>
void process(T t, U u) {}

int test() {
    process(10, 20);
    return 0;
}

// CIR: $_Z7processIiiEvT_T0_ = comdat any
// CIR: define dso_local noundef i32 @_Z4testv()
// CIR: define linkonce_odr void @_Z7processIiiEvT_T0_(i32

// OGCG: $_Z7processIiiEvT_T0_ = comdat any
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG: define linkonce_odr void @_Z7processIiiEvT_T0_(i32
