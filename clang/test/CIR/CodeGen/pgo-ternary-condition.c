// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -O0 \
// RUN:   -fprofile-instrument=clang -emit-llvm -o - %s | FileCheck %s

// A ?: (conditional operator) used as an if/loop condition must increment its
// region counter. CIR special-cased ?: conditions with a nested-branch fast
// path (in both emitIfOnBoolExpr and emitOpOnBoolExpr) that never incremented
// the conditional operator's counter, leaving it at zero. Under profile
// instrumentation the condition is now materialized via the scalar emitter,
// which instruments the counter while still evaluating only the taken branch.

int tern_if(int a, int b) {
  int r = 0;
  if (a > 0 ? b > 0 : b < 0)
    r = 1;
  return r;
}
// Both the conditional-operator region counter (index 1) and the if-then
// counter (index 2) must be incremented.
// CHECK-LABEL: define dso_local i32 @tern_if(
// CHECK-DAG: load i64, ptr @__profc_tern_if,
// CHECK-DAG: ptr @__profc_tern_if, i32 0, i32 1
// CHECK-DAG: ptr @__profc_tern_if, i32 0, i32 2
