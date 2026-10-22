// -emit-mlir= works when call-conv lowering never runs: the CIR-to-CIR
// pipeline runs once, however far the requested output is.

// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -fno-clangir-call-conv-lowering -emit-mlir=llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// LLVM: llvm.func {{.*}}@_ZTW1t()
// LLVM-NOT: cir.stage

struct A {
  A();
  ~A();
  int x;
};

thread_local A t;

int useT() { return t.x; }
