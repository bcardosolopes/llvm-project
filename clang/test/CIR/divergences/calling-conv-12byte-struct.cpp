// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// 12-byte structs (three ints) are coerced to { i64, i32 } per the x86_64
// SysV ABI: the first 8 bytes go in the low eightbyte (i64) and the next 4
// bytes go in the high eightbyte (i32).

struct ThreeInts {
    int a, b, c;  // 12 bytes total
};

ThreeInts return_three_ints() {
    return {1, 2, 3};
}

int take_three_ints(ThreeInts s) {
    return s.a + s.b + s.c;
}

int test() {
    ThreeInts s = return_three_ints();
    return take_three_ints(s);
}

// CIR: define dso_local { i64, i32 } @_Z17return_three_intsv()
// CIR: define dso_local noundef i32 @_Z15take_three_ints9ThreeInts(i64 {{.*}}, i32

// OGCG: define dso_local { i64, i32 } @_Z17return_three_intsv()
// OGCG: define dso_local noundef i32 @_Z15take_three_ints9ThreeInts(i64 {{.*}}, i32
