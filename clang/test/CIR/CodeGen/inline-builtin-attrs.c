// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: inline builtin redefinition declarations
// (e.g. memcpy with __always_inline__+__gnu_inline__) must have alwaysinline
// and noundef attributes on the declaration, matching OGCG.

typedef unsigned long size_t;
extern inline __attribute__((__always_inline__,__gnu_inline__))
void *memcpy(void *dst, const void *src, size_t n) {
  return __builtin_memcpy(dst, src, n);
}

void test(void *dst, const void *src) {
  memcpy(dst, src, 42);
}

// The memcpy declaration must have alwaysinline, nobuiltin, nounwind,
// and noundef on all parameters.
// CHECK: declare ptr @memcpy(ptr noundef, ptr noundef, i64 noundef) #[[MEMCPY_ATTRS:[0-9]+]]
// CHECK: attributes #[[MEMCPY_ATTRS]] = {{{.*}}alwaysinline{{.*}}nobuiltin{{.*}}nounwind{{.*}}}
