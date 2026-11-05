// RUN: %clang_cc1 -fclangir -emit-cir -std=c++20 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Test that VLA null/value initialization through emitNullInitialization
// emits a memset with dynamically-computed size.

// new int[n]() triggers emitNullInitialization for the VLA-like dynamic array.
void test_new_value_init(int n) {
  int *p = new int[n]();
  delete[] p;
}

// CIR: cir.libc.memset
// LLVM: call void @llvm.memset{{.*}}i8 0
