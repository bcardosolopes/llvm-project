// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Struct containing a single double (8 bytes) is classified SSE by the
// x86_64 SysV ABI and returned in an XMM register as `double`.

struct DoubleStruct {
    double d;  // 8 bytes, SSE class
};

DoubleStruct return_double() {
    return {3.14159};
}

int test() {
    DoubleStruct s = return_double();
    return s.d > 3.0 ? 1 : 0;
}

// CIR: define dso_local double @_Z13return_doublev()
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: define dso_local double @_Z13return_doublev()
// OGCG: define dso_local noundef i32 @_Z4testv()
