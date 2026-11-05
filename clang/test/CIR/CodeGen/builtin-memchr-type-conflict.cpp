// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -std=c++20 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
//
// Regression test: __builtin_char_memchr and
// __builtin_memchr both resolve to @memchr but with different C signatures
// (char* vs void*). CIR must emit @memchr with void* signature and bitcast
// char* arguments at the call site.

typedef unsigned long size_t;
const char* f_char(const char* s, char c, size_t n) {
  return __builtin_char_memchr(s, c, n);
}

const char* f_void(const char* s, char c, size_t n) {
  return static_cast<const char*>(__builtin_memchr(s, c, n));
}

// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR: cir.call @memchr(%{{.*}}, %{{.*}}, %{{.*}}){{.*}} : (!cir.ptr<!void>, !s32i, !u64i) -> !cir.ptr<!void>
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!void> -> !cir.ptr<!s8i>
// CIR-DAG: cir.func private @memchr(!cir.ptr<!void> {{.*}}, !s32i {{.*}}, !u64i {{.*}}) -> !cir.ptr<!void>

// LLVM: call ptr @memchr(ptr noundef %{{.*}}, i32 noundef %{{.*}}, i64 noundef %{{.*}})
// LLVM: declare ptr @memchr(ptr noundef, i32 noundef, i64 noundef)
