// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -Wno-unused-value -std=c++98 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s

// Ensure volatile side effects are preserved in discarded expressions.

volatile int i, j;
volatile _Complex int ci;

void test_volatile_cast_to_float() {
  // (float)(i=i) requires volatile read/store/re-read of i.
  (float)(i = i);
  // CHECK-LABEL: @_Z27test_volatile_cast_to_floatv
  // CHECK: load volatile i32
  // CHECK: store volatile i32
  // CHECK: load volatile i32
}

void test_volatile_complex_to_float() {
  // (float)(ci) requires volatile loads of both complex components.
  (float)(ci);
  // CHECK-LABEL: @_Z30test_volatile_complex_to_floatv
  // CHECK: load volatile i32
  // CHECK: load volatile i32
}

void test_volatile_negate_assign() {
  // -(i=j) requires volatile read/store/re-read.
  -(i = j);
  // CHECK-LABEL: @_Z27test_volatile_negate_assignv
  // CHECK: load volatile i32
  // CHECK: store volatile i32
  // CHECK: load volatile i32
}

void test_volatile_complex_bool() {
  // (bool)ci requires volatile loads of both complex components.
  (void)(bool)ci;
  // CHECK-LABEL: @_Z26test_volatile_complex_boolv
  // CHECK: load volatile i32
  // CHECK: load volatile i32
}
