// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(int, int);

void tile_constant_nested_loops(void) {
  // CHECK: cir.func{{.*}} @tile_constant_nested_loops
#pragma omp tile sizes(2, 3)
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 12; ++j)
      body(i, j);
  // CHECK: cir.for
  // CHECK: cir.for
  // CHECK: cir.call @body
}

void tile_runtime_nested_loops(int n, int m, int a, int b) {
  // CHECK-LABEL: cir.func{{.*}} @tile_runtime_nested_loops
#pragma omp tile sizes(a, b)
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j)
      body(i, j);
  // CHECK: [".capture_expr.", init]
  // CHECK: cir.ternary
  // CHECK: cir.call @body
}

// LLVM-LABEL: define {{.*}} @tile_constant_nested_loops()
// LLVM:         call void @body(
// LLVM-LABEL: define {{.*}} @tile_runtime_nested_loops(
// LLVM:         call void @body(

// OGCG-LABEL: define {{.*}} @tile_constant_nested_loops()
// OGCG:         call void @body(
// OGCG-LABEL: define {{.*}} @tile_runtime_nested_loops(
// OGCG:         call void @body(
