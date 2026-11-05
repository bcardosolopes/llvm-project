// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Struct with single float (4 bytes) is classified SSE by the x86_64 SysV ABI
// and returned in an XMM register as `float`.

struct FloatStruct {
    float f;  // 4 bytes, SSE class
};

FloatStruct return_float() {
    return {3.14f};
}

int test() {
    FloatStruct s = return_float();
    return s.f > 3.0f ? 1 : 0;
}

// CIR: define dso_local float @_Z12return_floatv()
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: define dso_local float @_Z12return_floatv()
// OGCG: define dso_local noundef i32 @_Z4testv()
