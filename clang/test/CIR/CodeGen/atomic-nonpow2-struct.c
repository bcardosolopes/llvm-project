// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Test atomic operations on structs whose size is not a power of 2.
// These require padding to the next power-of-2 integer type.

struct S3 { char a, b, c; };  // 3 bytes -> padded to 4 bytes for atomic ops

void test_atomic_store_s3(void) {
    _Atomic(struct S3) as;
    struct S3 s = {'a', 'b', 'c'};
    __c11_atomic_store(&as, s, __ATOMIC_SEQ_CST);
}

// Both CIR and OGCG should:
// 1. Zero-initialize a temp alloca (for padding)
// 2. Copy the 3-byte struct into the 4-byte temp
// 3. Load i32 from the temp
// 4. Do an atomic store of the i32

// LLVM: define {{.*}}void @test_atomic_store_s3()
// LLVM: call void @llvm.memset.p0.i64(ptr align 4 %{{.*}}, i8 0, i64 4, i1 false)
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 1 %{{.*}}, i64 3, i1 false)
// LLVM: store atomic i32 {{.*}} seq_cst

// OGCG: define {{.*}}void @test_atomic_store_s3()
// OGCG: call void @llvm.memset.p0.i64(ptr align 4 %{{.*}}, i8 0, i64 4, i1 false)
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 1 %{{.*}}, i64 3, i1 false)
// OGCG: store atomic i32 {{.*}} seq_cst
