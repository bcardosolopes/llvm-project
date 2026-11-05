// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Template with non-type parameter. The instantiated method is emitted with
// comdat linkage (linkonce_odr) in both CIR and OGCG.

template<int N>
struct Array {
    int data[N];
    int size() const { return N; }
};

int test() {
    Array<5> arr;
    return arr.size();
}

// CIR: $_ZNK5ArrayILi5EE4sizeEv = comdat any
// CIR: define dso_local noundef i32 @_Z4testv()
// CIR: define linkonce_odr noundef i32 @_ZNK5ArrayILi5EE4sizeEv(ptr

// OGCG: $_ZNK5ArrayILi5EE4sizeEv = comdat any
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG: define linkonce_odr noundef i32 @_ZNK5ArrayILi5EE4sizeEv(ptr
