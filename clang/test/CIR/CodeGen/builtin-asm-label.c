// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that builtins with __asm__ labels use the asm label name.

typedef unsigned long size_t;
extern void *memcpy(void *, const void *, size_t) __asm__("__memcpy_chk");

void test(void *dst, const void *src, size_t n) {
  memcpy(dst, src, n);
}

// CIR: cir.call @__memcpy_chk
// LLVM: call {{.*}}@__memcpy_chk
// OGCG: call {{.*}}@__memcpy_chk
