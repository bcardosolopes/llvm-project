// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Structs containing arrays are classified by their element types. This 8-byte
// struct (array of 2 ints) is classified as INTEGER and coerced to i64 per the
// x86_64 SysV ABI.

struct ArrayStruct {
    int arr[2];  // 8 bytes
};

ArrayStruct return_array_struct() {
    return {{1, 2}};
}

int test() {
    ArrayStruct s = return_array_struct();
    return s.arr[0] + s.arr[1];
}

// CIR: define dso_local i64 @_Z19return_array_structv()

// OGCG: define dso_local i64 @_Z19return_array_structv()
