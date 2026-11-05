// RUN: %clang_cc1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that __attribute__((not_tail_called)) produces notail on call sites.

__attribute__((not_tail_called)) int callee(void);

int test_notail(void) {
  return callee();
}

// CHECK: notail call {{.*}} @callee()
