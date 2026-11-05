// RUN: %clang_cc1 -fopenmp -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:            -emit-cir %s -o - | FileCheck %s

// A worksharing loop nested directly inside an omp.target region (without an
// intervening teams/parallel region) needs its induction-variable alloca placed
// inside the target region. omp.target is IsolatedFromAbove, so an alloca in the
// function entry block would be referenced across the isolation boundary and
// fail verification with "op using value defined outside the region".

void f(int *a, int n) {
#pragma omp target map(tofrom : a[0 : n])
  {
#pragma omp parallel for
    for (int i = 0; i < n; i++)
      a[i] = i;
  }
}

// The induction-variable alloca must be emitted inside the omp.target region,
// before the nested omp.parallel that uses it.
// CHECK: omp.target
// CHECK: cir.alloca {{.*}} ["i"]
// CHECK: omp.parallel
// CHECK: omp.wsloop
