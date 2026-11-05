// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Packed structs (5 bytes on x86_64) cannot be passed in registers without
// padding and must use sret per the x86_64 SysV ABI Memory class rules.
// The packed attribute removes natural alignment, keeping the struct at 5 bytes.
//
// Note: CIR now emits `dead_on_unwind` and `writable` on sret parameters,
// matching OGCG behavior. These are optimizer hints, not ABI-required attributes.

struct __attribute__((packed)) PackedStruct {
    char c;    // 1 byte
    int i;     // 4 bytes, no padding - total 5 bytes packed
};

PackedStruct return_packed() {
    return {1, 2};
}

int test() {
    PackedStruct s = return_packed();
    return s.i;
}

// CIR: define dso_local void @_Z13return_packedv(ptr dead_on_unwind noalias writable sret(%struct.PackedStruct) align 1
// CIR: define dso_local noundef i32 @_Z4testv()
// CIR:   call void @_Z13return_packedv(ptr

// OGCG: define dso_local void @_Z13return_packedv(ptr dead_on_unwind noalias writable sret(%struct.PackedStruct) align 1
// OGCG: define dso_local noundef i32 @_Z4testv()
// OGCG:   call void @_Z13return_packedv(ptr
