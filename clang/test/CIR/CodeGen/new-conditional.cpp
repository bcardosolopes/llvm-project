// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s

// Test that new-expression inside a conditional branch correctly pushes
// the delete cleanup (enterNewDeleteCleanup conditional branch case).

struct S {
  S();
  ~S();
};

// CHECK-LABEL: cir.func{{.*}} @_Z20test_conditional_newb
// CHECK:   cir.ternary
// CHECK:     cir.call @_Znwm
// CHECK:     cir.try synthetic cleanup
// CHECK:       cir.call exception @_ZN1SC1Ev
// CHECK:       cleanup {
// CHECK:         cir.call @_ZdlPvm
S *test_conditional_new(bool cond) {
  return cond ? new S() : nullptr;
}

// LLVM-LABEL: define{{.*}} ptr @_Z20test_conditional_newb
// LLVM:   invoke void @_ZN1SC1Ev
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   call void @_ZdlPvm
// LLVM:   resume
