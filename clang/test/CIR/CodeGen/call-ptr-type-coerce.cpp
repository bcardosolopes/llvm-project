// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Test that pointer type mismatches between call arguments and callee
// parameter types are handled via bitcast coercion in emitCallLikeOp.
// CIR uses typed pointers (unlike LLVM IR's opaque pointers), so
// void* vs char* requires explicit bitcasts at call sites.
// This verifies the fix: cir.call operand type mismatch.

typedef unsigned long size_t;
extern "C" void *memchr(const void *s, int c, size_t n);

template <class T>
T *find_char(T *str, int val, size_t count) {
  return static_cast<T*>(__builtin_memchr(str, val, count));
}

const char *test(const char *s, size_t n) {
  return find_char(s, 'a', n);
}

// Verify CIR compiles without type mismatch errors.
// CHECK: cir.call @memchr
// CHECK: cir.func private @memchr(
