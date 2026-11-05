// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Structs containing a single pointer (8 bytes on x86_64) are classified as
// INTEGER by the x86_64 SysV ABI and the field type is preserved as `ptr`
// rather than widened to `i64`.

struct PtrStruct {
    int* ptr;  // 8 bytes on x86_64
};

PtrStruct return_ptr_struct() {
    static int x = 42;
    return {&x};
}

int test() {
    PtrStruct s = return_ptr_struct();
    return *s.ptr;
}

// CIR: define dso_local ptr @_Z17return_ptr_structv()

// OGCG: define dso_local ptr @_Z17return_ptr_structv()
