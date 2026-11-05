// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Nested structs follow the same x86_64 SysV ABI classification as flat
// structs: each eightbyte is classified independently. This 16-byte nested
// struct coerces to { i64, i64 }.

struct Inner {
    int x, y;  // 8 bytes
};

struct Outer {
    Inner i1, i2;  // 16 bytes total
};

Outer return_nested() {
    return {{1, 2}, {3, 4}};
}

int test() {
    Outer o = return_nested();
    return o.i1.x + o.i2.y;
}

// CIR: define dso_local { i64, i64 } @_Z13return_nestedv()

// OGCG: define dso_local { i64, i64 } @_Z13return_nestedv()
