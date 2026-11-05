// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
//
// Ensure each static local initialized with &&label is tied to the correct
// function-local block label even when multiple functions use the same label
// spelling.

void f1(void) {
A:;
  static void *p = &&A;
}

void f2(void) {
A:;
  static void *q = &&A;
}

// LLVM-DAG: @f1.p = internal global ptr blockaddress(@f1,
// LLVM-DAG: @f2.q = internal global ptr blockaddress(@f2,
// LLVM-NOT: @f1.p = internal global ptr blockaddress(@f2,
// LLVM-NOT: @f2.q = internal global ptr blockaddress(@f1,
