// RUN: %clang_cc1 -std=c++20 -triple aarch64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that the calling convention lowering pass does not crash when rewriting
// a TryCallOp (i.e. a function call inside a try block that can throw). This
// is a regression test for a bug where rewriteCallOp(LowerFunctionInfo, ...)
// accessed the null class member 'callOp' instead of the 'Caller' parameter,
// causing a segfault on getODSResultIndexAndLength.

struct S {
  int a;
  int b;
};

S may_throw(S s);

void may_throw_void(S s);

// Test non-void return: the struct is coerced for AArch64 CC lowering.
// CHECK-LABEL: define{{.*}} void @_Z4testv
// CHECK: invoke {{.*}} @_Z9may_throw1S
void test() {
  try {
    S s{1, 2};
    S r = may_throw(s);
  } catch (...) {
  }
}

// Test void return: ensures TryCallOp lowering works when there is no result.
// CHECK-LABEL: define{{.*}} void @_Z10test_void2v
// CHECK: invoke void @_Z14may_throw_void1S
void test_void2() {
  try {
    S s{3, 4};
    may_throw_void(s);
  } catch (...) {
  }
}
