// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-direct-lowering -emit-mlir=core %s -o %t.mlir
// RUN: FileCheck --input-file=%t.mlir %s
//
// Previously rejected: for loop with comma operator in increment was not
// legalized through MLIR. Now compiles successfully.

void f() {}

void reject() {
  for (int i = 0; i < 100; i++, f());
  // CHECK: scf.while
}
