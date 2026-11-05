// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(int, int);

void stripe_constant_nested_loops(void) {
  // CHECK: cir.func{{.*}} @stripe_constant_nested_loops
#pragma omp stripe sizes(2, 3)
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 12; ++j)
      body(i, j);
  // CHECK: cir.for
  // CHECK: cir.for
  // CHECK: cir.call @body
}

void stripe_runtime_nested_loops(int n, int m, int a, int b) {
  // CHECK-LABEL: cir.func{{.*}} @stripe_runtime_nested_loops
#pragma omp stripe sizes(a, b)
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j)
      body(i, j);
  // CHECK: [".capture_expr.", init]
  // CHECK: cir.ternary
  // CHECK: cir.call @body
}

// LLVM-LABEL: define {{.*}} @stripe_constant_nested_loops()
// LLVM:         call void @body(
// LLVM-LABEL: define {{.*}} @stripe_runtime_nested_loops(
// LLVM:         call void @body(

// OGCG-LABEL: define {{.*}} @stripe_constant_nested_loops()
// OGCG:         call void @body(
// OGCG-LABEL: define {{.*}} @stripe_runtime_nested_loops(
// OGCG:         call void @body(
