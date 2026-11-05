// RUN: %clang_cc1 -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that unused implicit copy/move assignment operators are not emitted.
// Previously CIR would eagerly emit these even when never called.

struct S {
  int x;
};

void test() {
  S a;
  a.x = 42;
  // No copy or assignment — implicit operator= should NOT be emitted.
}

// CHECK: define {{.*}} @_Z4testv
// CHECK-NOT: @_ZN1SaSERKS_
// CHECK-NOT: @_ZN1SaSEOS_
