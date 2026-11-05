// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Empty structs have size 1 in C++ (cannot be zero-sized), but per the
// x86_64 SysV ABI they are classified as NoClass and ignored in parameter
// passing (both as return values and as function arguments).
// Fixed: CIR was incorrectly treating the implicit i8 padding byte as a
// real data member and classifying the struct as Integer rather than NoClass.

struct EmptyStruct {};

EmptyStruct return_empty() {
    return {};
}

void take_empty(EmptyStruct s) {}

int test() {
    EmptyStruct s = return_empty();
    take_empty(s);
    return 0;
}

// Empty structs are ignored: return_empty returns void, take_empty takes
// no args (the EmptyStruct argument is dropped).
// CIR: define dso_local void @_Z12return_emptyv()
// CIR: define dso_local void @_Z10take_empty11EmptyStruct()
// CIR: call void @_Z12return_emptyv()
// CIR: call void @_Z10take_empty11EmptyStruct()

// OGCG: define dso_local void @_Z12return_emptyv()
// OGCG: define dso_local void @_Z10take_empty11EmptyStruct()
// OGCG: call void @_Z12return_emptyv()
// OGCG: call void @_Z10take_empty11EmptyStruct()
