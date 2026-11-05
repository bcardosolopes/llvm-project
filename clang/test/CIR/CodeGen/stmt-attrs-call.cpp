// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that [[clang::noinline]], [[clang::always_inline]], and
// [[clang::nomerge]] statement attributes are emitted as call-site
// extra attributes on cir.call ops.

void callee();

void test_noinline() {
  [[clang::noinline]] callee();
}

void test_alwaysinline() {
  [[clang::always_inline]] callee();
}

void test_nomerge() {
  [[clang::nomerge]] callee();
}

// Verify the three different attribute sets exist
// CHECK: = #cir<extra({noinline})>
// CHECK: = #cir<extra({alwaysinline})>
// CHECK: = #cir<extra({nomerge})>
