// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Over-aligned structs (alignas > natural alignment) affect sret alignment.
// AlignedStruct has 4 bytes of data but requires 32-byte alignment,
// so sizeof(AlignedStruct) = 32, which triggers MEMORY classification (>16B).
// x86_64 ABI: sret parameter alignment must match struct alignment.
//
// CIR now emits `dead_on_unwind` and `writable` attributes
// on the sret parameter, matching OGCG (these are optimizer hints, not ABI-required).

struct alignas(32) AlignedStruct {
    int x;  // 4 bytes but 32-byte aligned
};

AlignedStruct return_aligned() {
    return {42};
}

int test() {
    AlignedStruct s = return_aligned();
    return s.x;
}

// CIR: define dso_local void @_Z14return_alignedv(ptr dead_on_unwind noalias writable sret(%struct.AlignedStruct) align 32
// CIR: define dso_local noundef i32 @_Z4testv()
// CIR:   call void @_Z14return_alignedv(ptr

// OGCG: define dso_local void @_Z14return_alignedv(ptr dead_on_unwind noalias writable sret(%struct.AlignedStruct) align 32
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG:   call void @_Z14return_alignedv(ptr
