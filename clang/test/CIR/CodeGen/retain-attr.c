// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that __attribute__((retain)) emits @llvm.used.

__attribute__((retain)) int retained_var = 42;

// LLVM: @llvm.used = appending global [1 x ptr] [ptr @retained_var]
// OGCG: @llvm.used = appending global [1 x ptr] [ptr @retained_var]
