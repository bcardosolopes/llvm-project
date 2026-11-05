// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(int, int);

void interchange_constant_nested_loops(void) {
  // CHECK: cir.func{{.*}} @interchange_constant_nested_loops
#pragma omp interchange
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 12; ++j)
      body(i, j);
  // CHECK: cir.for
  // CHECK: cir.for
  // CHECK: cir.call @body
}

// LLVM-LABEL: define {{.*}} @interchange_constant_nested_loops()
// LLVM:         call void @body(

// OGCG-LABEL: define {{.*}} @interchange_constant_nested_loops()
// OGCG:         call void @body(
