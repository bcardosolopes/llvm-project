// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// 4-byte structs are coerced to i32 per the x86_64 SysV ABI (INTEGER class,
// fits in a single register). Both return and parameter passing use i32.

struct TinyStruct {
    int x;  // 4 bytes
};

TinyStruct return_tiny() {
    return {42};
}

int take_tiny(TinyStruct s) {
    return s.x;
}

int test() {
    TinyStruct s = return_tiny();
    return take_tiny(s);
}

// CIR: define dso_local i32 @_Z11return_tinyv()
// CIR: define dso_local noundef i32 @_Z9take_tiny10TinyStruct(i32

// OGCG: define dso_local i32 @_Z11return_tinyv()
// OGCG: define dso_local noundef i32 @_Z9take_tiny10TinyStruct(i32
