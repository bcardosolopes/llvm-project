// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// A struct containing a single long long (8 bytes) is classified as INTEGER
// and coerced to i64 per the x86_64 SysV ABI.

struct LongLongStruct {
    long long ll;  // 8 bytes
};

LongLongStruct return_longlong() {
    return {123456789LL};
}

int test() {
    LongLongStruct s = return_longlong();
    return s.ll > 0 ? 1 : 0;
}

// CIR: define dso_local i64 @_Z15return_longlongv()

// OGCG: define dso_local i64 @_Z15return_longlongv()
