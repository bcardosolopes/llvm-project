// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Structs larger than 16 bytes (two eightbytes) exceed register capacity and
// must be returned via a hidden sret pointer per the x86_64 SysV ABI.
// FiveInts is 20 bytes → class MEMORY → sret.
//
// Note: CIR now emits `dead_on_unwind` and `writable` on sret parameters,
// matching OGCG behavior. These are optimizer hints, not ABI-required attributes.

struct FiveInts {
    int a, b, c, d, e;  // 20 bytes - over the 16-byte register limit
};

FiveInts return_five_ints() {
    return {1, 2, 3, 4, 5};
}

int test() {
    FiveInts s = return_five_ints();
    return s.a + s.e;
}

// CIR: define dso_local void @_Z16return_five_intsv(ptr dead_on_unwind noalias writable sret(%struct.FiveInts) align 4
// CIR: define dso_local noundef i32 @_Z4testv()
// CIR:   call void @_Z16return_five_intsv(ptr

// OGCG: define dso_local void @_Z16return_five_intsv(ptr dead_on_unwind noalias writable sret(%struct.FiveInts) align 4
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG:   call void @_Z16return_five_intsv(ptr
