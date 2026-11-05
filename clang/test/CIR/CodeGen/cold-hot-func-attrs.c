// RUN: %clang_cc1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that __attribute__((cold)) is propagated to the function declaration
// in LLVM IR. Previously, 'cold' was only on call sites, not declarations.

__attribute__((cold)) void cold_func(void);
__attribute__((hot)) void hot_func(void);

void test(int x) {
  if (x > 100)
    hot_func();
  else
    cold_func();
}

// CHECK: ; Function Attrs:{{.*}}cold
// CHECK: declare void @cold_func()
// CHECK: ; Function Attrs:{{.*}}hot
// CHECK: declare void @hot_func()
