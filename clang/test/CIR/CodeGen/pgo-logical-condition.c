// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -O0 \
// RUN:   -fprofile-instrument=clang -emit-llvm -o - %s | FileCheck %s

// A && / || used as an if (or loop) condition must still increment its region
// and condition counters. CIR's short-circuit branch emission for if-conditions
// nested the operands as branches but never incremented the &&/|| counters, so
// they stayed zero (only the function-entry and if-then counters fired). Under
// profile instrumentation the condition is now materialized via the scalar
// emitter, which instruments every counter while preserving short-circuiting.

int land_if(int a, int b) {
  int r = 0;
  if (a > 0 && b > 0)
    r = 1;
  return r;
}
// All four counters of @__profc_land_if must be incremented (indices 0..3).
// CHECK-LABEL: define dso_local i32 @land_if(
// CHECK-DAG: load i64, ptr @__profc_land_if,
// CHECK-DAG: ptr @__profc_land_if, i32 0, i32 1
// CHECK-DAG: ptr @__profc_land_if, i32 0, i32 2
// CHECK-DAG: ptr @__profc_land_if, i32 0, i32 3

int lor_if(int a, int b) {
  int r = 0;
  if (a > 0 || b > 0)
    r = 1;
  return r;
}
// CHECK-LABEL: define dso_local i32 @lor_if(
// CHECK-DAG: load i64, ptr @__profc_lor_if,
// CHECK-DAG: ptr @__profc_lor_if, i32 0, i32 1
// CHECK-DAG: ptr @__profc_lor_if, i32 0, i32 2
// CHECK-DAG: ptr @__profc_lor_if, i32 0, i32 3
