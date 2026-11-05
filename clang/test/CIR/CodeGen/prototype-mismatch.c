// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// K&R function with prototype declaration — call should be non-variadic.

static int foo(int);

static int foo(C)
char C;
{
  return C;
}

void test(void) {
  foo(7);
}

// CIR: cir.call @foo({{%.*}}) : (!s32i) -> !s32i
// CIR-NOT: cir.cast bitcast

// LLVM: call i32 @foo(i32 noundef 7)
// LLVM-NOT: call i32 (i32, ...)

// OGCG: call i32 @foo(i32 noundef 7)
// OGCG-NOT: call i32 (i32, ...)
