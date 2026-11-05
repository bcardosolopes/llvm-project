// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// 16-byte structs are at the upper register-pair boundary. Per the x86_64
// SysV ABI they are coerced to { i64, i64 } (two INTEGER eightbytes).

struct FourInts {
    int a, b, c, d;  // 16 bytes - boundary case
};

FourInts return_four_ints() {
    return {1, 2, 3, 4};
}

int take_four_ints(FourInts s) {
    return s.a + s.b + s.c + s.d;
}

int test() {
    FourInts s = return_four_ints();
    return take_four_ints(s);
}

// CIR: define dso_local { i64, i64 } @_Z16return_four_intsv()
// CIR: define dso_local noundef i32 @_Z14take_four_ints8FourInts(i64 {{.*}}, i64

// OGCG: define dso_local { i64, i64 } @_Z16return_four_intsv()
// OGCG: define dso_local noundef i32 @_Z14take_four_ints8FourInts(i64 {{.*}}, i64
