// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Multiple struct parameters each follow ABI rules independently. Two 8-byte
// structs (Pair) are each coerced to i64 per the x86_64 SysV ABI.

struct Pair {
    int a, b;  // 8 bytes
};

int take_two_structs(Pair p1, Pair p2) {
    return p1.a + p2.b;
}

int test() {
    return take_two_structs({1, 2}, {3, 4});
}

// CIR: define dso_local noundef i32 @_Z16take_two_structs4PairS_(i64 {{.*}}, i64

// OGCG: define dso_local noundef i32 @_Z16take_two_structs4PairS_(i64 {{.*}}, i64
