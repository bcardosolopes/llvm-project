// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-cir %s -o - | FileCheck %s

// On the `taskloop simd` composite the reduction must be carried on the inner
// `omp.simd` op, not only on the `omp.taskloop` op. Otherwise the simd
// vectorizer stamps parallel-access metadata over a loop whose body performs a
// genuine reduction, producing a deterministically wrong result.

long f(void) {
  long s = 0;
#pragma omp parallel num_threads(4)
#pragma omp single
#pragma omp taskloop simd reduction(+ : s) grainsize(10)
  for (int i = 0; i < 100; i++)
    s += i;
  return s;
}

// CHECK: omp.taskloop {{.*}}reduction(
// CHECK: omp.simd reduction(
