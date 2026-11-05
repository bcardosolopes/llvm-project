// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(int);

void reverse_constant_step(void) {
  // CHECK: cir.func{{.*}} @reverse_constant_step
#pragma omp reverse
  for (int i = 7; i < 17; i += 3)
    body(i);
  // CHECK: cir.for
  // CHECK: cir.call @body
}

void reverse_runtime_step(int start, int end, int step) {
  // CHECK-LABEL: cir.func{{.*}} @reverse_runtime_step
#pragma omp reverse
  for (int i = start; i < end; i += step)
    body(i);
  // CHECK: [".capture_expr.", init]
  // CHECK: [".new_step", init]
  // CHECK: cir.for
  // CHECK: cir.call @body
}

// LLVM-LABEL: define {{.*}} @reverse_constant_step()
// LLVM:         call void @body(
// LLVM-LABEL: define {{.*}} @reverse_runtime_step(
// LLVM:         call void @body(

// OGCG-LABEL: define {{.*}} @reverse_constant_step()
// OGCG:         call void @body(
// OGCG-LABEL: define {{.*}} @reverse_runtime_step(
// OGCG:         call void @body(
