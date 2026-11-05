// RUN: %clang_cc1 -std=c23 -triple x86_64-gnu-linux -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// _BitInt constant aggregate oversized assertion

struct S1 {
  int x;
  _BitInt(187) y;
};

void foo(void) {
  struct S1 A = {1, 0xAA};
}

// LLVM: @__const.foo.A = private unnamed_addr constant
