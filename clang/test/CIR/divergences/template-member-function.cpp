// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Member function template instantiation is emitted with comdat linkage
// (linkonce_odr) in both CIR and OGCG.

struct S {
    template<typename T>
    int process(T value) {
        return static_cast<int>(value);
    }
};

int test() {
    S s;
    return s.process(42.5);
}

// CIR: $_ZN1S7processIdEEiT_ = comdat any
// CIR: define dso_local noundef i32 @_Z4testv()
// CIR: define linkonce_odr noundef i32 @_ZN1S7processIdEEiT_(ptr

// OGCG: $_ZN1S7processIdEEiT_ = comdat any
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG: define linkonce_odr noundef i32 @_ZN1S7processIdEEiT_(ptr
