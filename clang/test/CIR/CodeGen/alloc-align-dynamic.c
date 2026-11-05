// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test dynamic alignment from __attribute__((alloc_align(N))).
// The alignment value comes from a call argument at runtime.
// OGCG always casts the alignment operand to IntPtrTy (i64 on 64-bit targets).

void *my_aligned_alloc(int size, int alignment) __attribute__((alloc_align(2)));
void *my_aligned_alloc128(int size, __int128 alignment) __attribute__((alloc_align(2)));

// i32 dynamic alignment: should be zero-extended to i64.
void *test_dynamic_i32(int align) {
  return my_aligned_alloc(100, align);
}

// CIR: cir.assume_aligned %{{.*}} [align_value %{{.*}} : !s32i] : !cir.ptr<!void>
// LLVM: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 %{{.*}}) ]
// OGCG: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 %{{.*}}) ]

// i128 dynamic alignment: should be truncated to i64.
void *test_dynamic_i128(__int128 align) {
  return my_aligned_alloc128(100, align);
}

// CIR: cir.assume_aligned %{{.*}} [align_value %{{.*}} : !s128i] : !cir.ptr<!void>
// LLVM: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 %{{.*}}) ]
// OGCG: call void @llvm.assume(i1 true) [ "align"(ptr %{{.*}}, i64 %{{.*}}) ]

// Constant alignment should use the attribute path (align N on return).
void *test_constant(void) {
  return my_aligned_alloc(100, 16);
}

// CIR: cir.assume_aligned %{{.*}} alignment 16 : !cir.ptr<!void>
