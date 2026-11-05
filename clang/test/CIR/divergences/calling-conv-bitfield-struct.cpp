// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Structs with bitfields are classified by their underlying storage type.
// This struct fits in 4 bytes and is classified as INTEGER, coerced to i32.

struct BitfieldStruct {
    unsigned int a : 3;  // 3 bits
    unsigned int b : 5;  // 5 bits
    // Total: 1 byte with padding to 4 bytes
};

BitfieldStruct return_bitfield() {
    return {1, 2};
}

int test() {
    BitfieldStruct s = return_bitfield();
    return s.a + s.b;
}

// CIR: define dso_local i32 @_Z15return_bitfieldv()

// OGCG: define dso_local i32 @_Z15return_bitfieldv()
