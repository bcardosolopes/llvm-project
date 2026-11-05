// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Structs containing bool fields are classified as INTEGER. This struct has
// bool (1 byte) + padding + int (4 bytes) = 8 bytes total, coerced to i64.

struct BoolStruct {
    bool b;    // 1 byte
    int x;     // 4 bytes (with 3 bytes padding before it = 8 bytes total)
};

BoolStruct return_bool_struct() {
    return {true, 42};
}

int test() {
    BoolStruct s = return_bool_struct();
    return s.b ? s.x : 0;
}

// CIR: define dso_local i64 @_Z18return_bool_structv()

// OGCG: define dso_local i64 @_Z18return_bool_structv()
