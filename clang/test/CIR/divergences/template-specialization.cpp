// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Template specialization. The specialized static member function is emitted
// with comdat linkage (linkonce_odr) in both CIR and OGCG.

template<typename T>
struct Value {
    static int get() { return 1; }
};

template<>
struct Value<int> {
    static int get() { return 42; }
};

int test() {
    return Value<int>::get();
}

// CIR: $_ZN5ValueIiE3getEv = comdat any
// CIR: define linkonce_odr noundef i32 @_ZN5ValueIiE3getEv()
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: $_ZN5ValueIiE3getEv = comdat any
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG: define linkonce_odr noundef i32 @_ZN5ValueIiE3getEv()
