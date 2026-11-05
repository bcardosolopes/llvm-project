// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++20 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that CIR inserts bitcasts from typed pointers to void* when calling
// memory builtins. CIR uses typed pointers, so MemCpyOp/MemMoveOp/MemSetOp
// require explicit bitcasts to !cir.ptr<!cir.void>. OGCG never needs this
// because LLVM uses opaque pointers.

// CIR-LABEL: @_Z12test_memmove
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!u8i> -> !cir.ptr<!void>
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!u8i> -> !cir.ptr<!void>
// CIR: cir.libc.memmove {{.*}} !cir.ptr<!void> -> !cir.ptr<!void>
// LLVM-LABEL: @_Z12test_memmove
// LLVM: call void @llvm.memmove
void *test_memmove(unsigned char *dst, const unsigned char *src, unsigned long n) {
  return __builtin_memmove(dst, src, n);
}

// CIR-LABEL: @_Z11test_memcpy
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR: cir.libc.memcpy {{.*}} : {{.*}}, !cir.ptr<!void>
// LLVM-LABEL: @_Z11test_memcpy
// LLVM: call void @llvm.memcpy
void *test_memcpy(char *dst, const char *src, unsigned long n) {
  return __builtin_memcpy(dst, src, n);
}

// CIR-LABEL: @_Z11test_memset
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!s32i> -> !cir.ptr<!void>
// CIR: cir.libc.memset {{.*}} : !cir.ptr<!void>
// LLVM-LABEL: @_Z11test_memset
// LLVM: call void @llvm.memset
void *test_memset(int *dst, int val, unsigned long n) {
  return __builtin_memset(dst, val, n);
}

// CIR-LABEL: @_Z10test_bzero
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!s32i> -> !cir.ptr<!void>
// CIR: cir.libc.memset {{.*}} : !cir.ptr<!void>
// LLVM-LABEL: @_Z10test_bzero
// LLVM: call void @llvm.memset
void test_bzero(int *dst, unsigned long n) {
  __builtin_bzero(dst, n);
}
