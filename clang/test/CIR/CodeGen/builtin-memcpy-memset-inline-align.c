// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

void test_memcpy_inline(void *dst, const void *src) {
  __builtin_memcpy_inline(dst, src, 4);
}

// LLVM-LABEL: define {{.*}}void @test_memcpy_inline
// LLVM: call void @llvm.memcpy.inline.p0.p0.i64(ptr align 1 {{.*}}, ptr align 1 {{.*}}, i64 4, i1 false)

// OGCG-LABEL: define {{.*}}void @test_memcpy_inline
// OGCG: call void @llvm.memcpy.inline.p0.p0.i64(ptr align 1 {{.*}}, ptr align 1 {{.*}}, i64 4, i1 false)

void test_memset_inline(void *dst) {
  __builtin_memset_inline(dst, 0, 8);
}

// LLVM-LABEL: define {{.*}}void @test_memset_inline
// LLVM: call void @llvm.memset.inline.p0.i64(ptr align 1 {{.*}}, i8 0, i64 8, i1 false)

// OGCG-LABEL: define {{.*}}void @test_memset_inline
// OGCG: call void @llvm.memset.inline.p0.i64(ptr align 1 {{.*}}, i8 0, i64 8, i1 false)

typedef struct __attribute__((aligned(8))) { char data[16]; } aligned_buf;

void test_aligned_memcpy_inline(aligned_buf *dst, const aligned_buf *src) {
  __builtin_memcpy_inline(dst, src, 16);
}

// LLVM-LABEL: define {{.*}}void @test_aligned_memcpy_inline
// LLVM: call void @llvm.memcpy.inline.p0.p0.i64(ptr align 8 {{.*}}, ptr align 8 {{.*}}, i64 16, i1 false)

// OGCG-LABEL: define {{.*}}void @test_aligned_memcpy_inline
// OGCG: call void @llvm.memcpy.inline.p0.p0.i64(ptr align 8 {{.*}}, ptr align 8 {{.*}}, i64 16, i1 false)

void test_aligned_memset_inline(aligned_buf *dst) {
  __builtin_memset_inline(dst, 0, 16);
}

// LLVM-LABEL: define {{.*}}void @test_aligned_memset_inline
// LLVM: call void @llvm.memset.inline.p0.i64(ptr align 8 {{.*}}, i8 0, i64 16, i1 false)

// OGCG-LABEL: define {{.*}}void @test_aligned_memset_inline
// OGCG: call void @llvm.memset.inline.p0.i64(ptr align 8 {{.*}}, i8 0, i64 16, i1 false)
