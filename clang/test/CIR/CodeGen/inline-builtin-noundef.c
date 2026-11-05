// RUN: %clang_cc1 -triple x86_64-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that inline builtin definitions (.inline clones) do NOT have
// noundef on their definition parameters, but call sites DO have noundef.
// OGCG creates .inline clones via llvm::Function::Create which starts
// with no parameter attributes.

int abs(int);
extern inline __attribute__((always_inline, gnu_inline)) int abs(int x) {
  return __builtin_abs(x);
}

int test(void) {
  return abs(0);
}

// CIR: define internal i32 @abs.inline(i32 %
// CIR: call i32 @abs.inline(i32 noundef

// OGCG: call i32 @abs.inline(i32 noundef
// OGCG: define internal i32 @abs.inline(i32 %
