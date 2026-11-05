// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
//
// Regression test: CIR verification used to fail when both
// __builtin_char_memchr and __builtin_memchr appeared in the same TU
// because they shared the @memchr symbol but had different pointer types
// (char* vs void*). The fix ensures @memchr always uses void* signature
// and __builtin_char_memchr emits bitcasts at the call site.

typedef unsigned long size_t;

const char* test_char_memchr(const char* s, char c, size_t n) {
  return __builtin_char_memchr(s, c, n);
}

const char* test_void_memchr(const char* s, char c, size_t n) {
  return static_cast<const char*>(__builtin_memchr(s, c, n));
}

// CIR-LABEL: @_Z16test_char_memchrPKccm
// CIR: cir.call @memchr({{.*}}){{.*}} : (!cir.ptr<!void>, !s32i, !u64i) -> !cir.ptr<!void>
// CIR: cir.func private @memchr(!cir.ptr<!void> {{.*}}, !s32i {{.*}}, !u64i {{.*}}) -> !cir.ptr<!void>
// CIR-LABEL: @_Z16test_void_memchrPKccm
// CIR: cir.call @memchr({{.*}}){{.*}} : (!cir.ptr<!void>, !s32i, !u64i) -> !cir.ptr<!void>

// LLVM-LABEL: @_Z16test_char_memchrPKccm
// LLVM: call ptr @memchr(ptr noundef %{{.*}}, i32 noundef %{{.*}}, i64 noundef %{{.*}})
// LLVM-LABEL: @_Z16test_void_memchrPKccm
// LLVM: call ptr @memchr(ptr noundef %{{.*}}, i32 noundef %{{.*}}, i64 noundef %{{.*}})
