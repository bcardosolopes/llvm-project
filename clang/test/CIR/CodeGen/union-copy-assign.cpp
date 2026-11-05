// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++17 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that trivial union copy-assignment lowers directly to cir.copy.
// Regression test.

union IntOrFloat {
  int i;
  float f;
};

// CHECK-LABEL: @_Z15test_union_copy
// CHECK: cir.copy
void test_union_copy() {
  IntOrFloat a;
  a.i = 42;
  IntOrFloat b;
  b = a;
}
