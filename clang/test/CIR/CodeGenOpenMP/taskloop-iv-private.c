// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-cir %s -o - | FileCheck %s

// The taskloop induction variable must be privatized per task through the
// taskloop's `omp.private` clause (mirroring classic clang's
// EmitOMPPrivateLoopCounters). Otherwise its alloca is captured shared into
// every task's context and all tasks race on one `i`.

void body(int);

void f(void) {
#pragma omp parallel num_threads(4)
#pragma omp single
#pragma omp taskloop grainsize(5)
  for (int i = 0; i < 100; i++)
    body(i);
}

// CHECK: omp.private {type = private} @__omp_taskloop_iv_i
// CHECK: omp.taskloop {{.*}}private(@__omp_taskloop_iv_i
