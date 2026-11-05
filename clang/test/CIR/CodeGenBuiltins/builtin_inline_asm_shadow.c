// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

typedef unsigned long size_t;

// Shadowing case with asm label: ensure non-inline definition wins and no
// transient *.inline symbol survives.
void *memmove(void *a, const void *b, size_t c);
extern __typeof(memmove) memmove __asm__("my_memmove_sym");

extern inline __attribute__((always_inline)) __attribute__((gnu_inline))
void *memmove(void *a, const void *b, size_t c) {
  return __builtin_memmove(a, b, c);
}

void *memmove(void *a, const void *b, size_t c) {
  char *dst = (char *)a;
  const char *src = (const char *)b;
  if (dst < src) {
    for (size_t i = 0; i < c; ++i)
      dst[i] = src[i];
  }
  return a;
}

void *test_shadowed_memmove_asm(void *a, const void *b, size_t c) {
  return memmove(a, b, c);
}

// CIR-LABEL: @my_memmove_sym(
// CIR-NOT: @my_memmove_sym.inline
// CIR-LABEL: @test_shadowed_memmove_asm(
// CIR: cir.call @my_memmove_sym
// CIR-NOT: @my_memmove_sym.inline

// LLVM-LABEL: @my_memmove_sym(
// LLVM-NOT: @my_memmove_sym.inline
// LLVM-LABEL: @test_shadowed_memmove_asm(
// LLVM: call ptr @my_memmove_sym(
// LLVM-NOT: @my_memmove_sym.inline

// OGCG-LABEL: @my_memmove_sym(
// OGCG-NOT: @my_memmove_sym.inline
// OGCG-LABEL: @test_shadowed_memmove_asm(
// OGCG: call ptr @my_memmove_sym(
// OGCG-NOT: @my_memmove_sym.inline
