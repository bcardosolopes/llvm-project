// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -O0 \
// RUN:   -fprofile-instrument=clang -emit-llvm -o - %s | FileCheck %s

// Profiling globals: counter arrays and data records.
// CHECK-DAG: @__profc_foo = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd_foo = private global
// CHECK-DAG: @__profc_loops = private global [4 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd_loops = private global
// CHECK-DAG: @__profc_logical_and = private global [3 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd_logical_and = private global
// CHECK-DAG: @__profc_logical_or = private global [3 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd_logical_or = private global
// CHECK-DAG: @__profc_ternary_conditional = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd_ternary_conditional = private global
// CHECK-DAG: @__profc_binary_conditional = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd_binary_conditional = private global

int foo(int x) {
  // Function entry counter (index 0).
  // CHECK-LABEL: define dso_local i32 @foo(
  // CHECK: %pgocount = load i64, ptr @__profc_foo
  if (x)
    // If-then counter (index 1).
    // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc_foo, i32 0, i32 1)
    return 1;
  return 0;
}

void loops(int n) {
  // Function entry counter (index 0).
  // CHECK-LABEL: define dso_local void @loops(
  // CHECK: %pgocount = load i64, ptr @__profc_loops
  for (int i = 0; i < n; i++) {
    // For-loop body counter (index 1).
    // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([4 x i64], ptr @__profc_loops, i32 0, i32 1)
  }
  while (n--) {
    // While-loop body counter (index 2).
    // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([4 x i64], ptr @__profc_loops, i32 0, i32 2)
  }
  do {
    // Do-while body counter (index 3).
    // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([4 x i64], ptr @__profc_loops, i32 0, i32 3)
  } while (--n > 0);
}

int logical_and(int a, int b) {
  // CHECK-LABEL: define dso_local i32 @logical_and(
  // CHECK: %pgocount = load i64, ptr @__profc_logical_and
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([3 x i64], ptr @__profc_logical_and, i32 0, i32 1)
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([3 x i64], ptr @__profc_logical_and, i32 0, i32 2)
  return a && b;
}

int logical_or(int a, int b) {
  // CHECK-LABEL: define dso_local i32 @logical_or(
  // CHECK: %pgocount = load i64, ptr @__profc_logical_or
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([3 x i64], ptr @__profc_logical_or, i32 0, i32 1)
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([3 x i64], ptr @__profc_logical_or, i32 0, i32 2)
  return a || b;
}

int ternary_conditional(int x) {
  // CHECK-LABEL: define dso_local i32 @ternary_conditional(
  // CHECK: %pgocount = load i64, ptr @__profc_ternary_conditional
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc_ternary_conditional, i32 0, i32 1)
  return x ? x : 4;
}

int binary_conditional(int x) {
  // CHECK-LABEL: define dso_local i32 @binary_conditional(
  // CHECK: %pgocount = load i64, ptr @__profc_binary_conditional
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc_binary_conditional, i32 0, i32 1)
  return x ?: 4;
}
