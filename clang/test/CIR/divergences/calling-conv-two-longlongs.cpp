// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Two long longs (16 bytes) are classified as two INTEGER eightbytes and
// coerced to { i64, i64 } per the x86_64 SysV ABI.

struct TwoLongLongs {
    long long a, b;  // 16 bytes
};

TwoLongLongs return_two_longlongs() {
    return {123LL, 456LL};
}

int test() {
    TwoLongLongs s = return_two_longlongs();
    return s.a + s.b > 0 ? 1 : 0;
}

// CIR: define dso_local { i64, i64 } @_Z20return_two_longlongsv()

// OGCG: define dso_local { i64, i64 } @_Z20return_two_longlongsv()
