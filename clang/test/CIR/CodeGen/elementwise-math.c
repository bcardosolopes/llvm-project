// RUN: %clang_cc1 -triple x86_64-apple-darwin -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

void test(char c) {
  char r = __builtin_elementwise_bitreverse(c);
}

// LLVM: call i8 @llvm.bitreverse.i8
