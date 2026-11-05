// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// Test: annotate with struct compound literal args now compiles (previously NYI).

struct S { int a; int b; };
int Y __attribute__((annotate("Test", 42, (struct S){1, 2})));

// LLVM: @llvm.global.annotations
