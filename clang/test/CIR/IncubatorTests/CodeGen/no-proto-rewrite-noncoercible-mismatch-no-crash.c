// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression: if a no-proto call argument type mismatches the eventual
// prototype but is not one of the pointer/method coercion cases, rewriting the
// call must still produce a valid module and keep the direct callee retarget.

int np_noncoercible();
int call_np_noncoercible(float x) { return np_noncoercible(x); }
int np_noncoercible(int y) { return y; }

// CHECK-LABEL: define dso_local i32 @call_np_noncoercible(
// CHECK: call i32 (double, ...) @np_noncoercible(double noundef
// CHECK-LABEL: define dso_local i32 @np_noncoercible(
