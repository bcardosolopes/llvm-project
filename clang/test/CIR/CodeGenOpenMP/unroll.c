// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(int);

void unroll_full_constant_step(void) {
  // CHECK: cir.func{{.*}} @unroll_full_constant_step
#pragma omp unroll full
  for (int i = 7; i < 17; i += 3)
    body(i);
  // CHECK: cir.for
  // CHECK: cir.call @body
  // CHECK: loopHint = {
  // CHECK-SAME: mustprogress = true
  // CHECK-SAME: unroll_enable = true
  // CHECK-SAME: unroll_full = true
}

void unroll_partial_factor(int n) {
  // CHECK: cir.func{{.*}} @unroll_partial_factor
#pragma omp unroll partial(4)
  for (int i = 0; i < n; i++)
    body(i);
  // CHECK: cir.for
  // CHECK: cir.call @body
  // CHECK: loopHint = {
  // CHECK-SAME: mustprogress = true
  // CHECK-SAME: unroll_count = 4
  // CHECK-SAME: unroll_enable = true
}

// Verify LLVM loop metadata for unroll directives.
// LLVM-LABEL: define {{.*}} @unroll_full_constant_step()
// LLVM:         !llvm.loop
// LLVM-LABEL: define {{.*}} @unroll_partial_factor(
// LLVM:         !llvm.loop
// LLVM: !"llvm.loop.unroll.full"
// LLVM: !"llvm.loop.unroll.count", i32 4

// OGCG-LABEL: define {{.*}} @unroll_full_constant_step()
// OGCG:         call void @body(
// OGCG:         !llvm.loop
// OGCG-LABEL: define {{.*}} @unroll_partial_factor(
// OGCG:         call void @body(
// OGCG:         !llvm.loop
// OGCG: !"llvm.loop.unroll.full"
// OGCG: !"llvm.loop.unroll.count", i32 4
