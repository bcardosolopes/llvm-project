// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.cir.ll -disable-llvm-passes
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that min-legal-vector-width is correctly updated from inline
// asm vector operands, matching OGCG behavior.

typedef long long __m64 __attribute__((__vector_size__(8)));

// Function without vector asm operands: width stays at 0.
int no_vector_asm(int x) {
  int result;
  __asm__ volatile("movl %1, %0" : "=r"(result) : "r"(x));
  return result;
}

// Function with 64-bit vector asm operand: width should be 64.
__m64 vector_asm(__m64 a, __m64 b) {
  __m64 result;
  __asm__ volatile("paddd %2, %0" : "=y"(result) : "0"(a), "y"(b));
  return result;
}

// LLVM: attributes {{.*}} "min-legal-vector-width"="0"
// LLVM: attributes {{.*}} "min-legal-vector-width"="64"

// OGCG: attributes {{.*}} "min-legal-vector-width"="0"
// OGCG: attributes {{.*}} "min-legal-vector-width"="64"
