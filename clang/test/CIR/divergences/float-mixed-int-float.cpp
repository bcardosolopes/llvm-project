// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Mixed int+float struct (8 bytes). The integer and float fields each occupy
// one eightbyte, but since they share the same eightbyte (both within bytes 0-7),
// the eightbyte class is INTEGER (integer class wins over SSE when mixed in the
// same eightbyte). Result: coerced to i64.

struct MixedStruct {
    int i;      // INTEGER class
    float f;    // SSE class
    // Total 8 bytes — one eightbyte → INTEGER wins
};

MixedStruct return_mixed() {
    return {42, 3.14f};
}

int test() {
    MixedStruct s = return_mixed();
    return s.i;
}

// CIR: define dso_local i64 @_Z12return_mixedv()
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: define dso_local i64 @_Z12return_mixedv()
// OGCG: define dso_local noundef i32 @_Z4testv()
