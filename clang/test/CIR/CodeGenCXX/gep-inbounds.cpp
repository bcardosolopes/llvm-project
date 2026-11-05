// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s

// CIR is missing the 'inbounds' flag on GEP instructions pervasively.

struct A {
  int x;
  int y;
};

struct B : A {
  int z;
  void f();
};

void test_member_gep(B *b) {
  b->y = 42;
  // CHECK-LABEL: @_Z15test_member_gepP1B
  // CHECK: getelementptr inbounds
}

void test_base_cast(B *b) {
  A *a = b;
  a->x = 1;
  // CHECK-LABEL: @_Z14test_base_castP1B
  // CHECK: getelementptr inbounds
}

int *test_array_gep(int *arr, int idx) {
  return &arr[idx];
  // CHECK-LABEL: @_Z14test_array_gep
  // CHECK: getelementptr inbounds
}
