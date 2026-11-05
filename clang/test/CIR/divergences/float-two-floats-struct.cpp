// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Struct with two floats (8 bytes total) is classified SSE by the x86_64
// SysV ABI and returned in an XMM register as <2 x float>.

struct TwoFloats {
    float a, b;  // 8 bytes, SSE class
};

TwoFloats return_two_floats() {
    return {1.0f, 2.0f};
}

int test() {
    TwoFloats s = return_two_floats();
    return s.a + s.b > 2.5f ? 1 : 0;
}

// CIR: define dso_local <2 x float> @_Z17return_two_floatsv()
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: define dso_local <2 x float> @_Z17return_two_floatsv()
// OGCG: define dso_local noundef i32 @_Z4testv()
