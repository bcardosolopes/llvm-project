// RUN: %clang_cc1 -verify -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -verify -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -verify -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o /dev/null
// expected-no-diagnostics

void for_simd_noncanonical(float *a) {
#pragma omp parallel
  {
#pragma omp for simd
    for (int i = 10; i > 1; --i)
      a[i] += 1.0f;
  }
}

struct S {
  S(int n) {
#pragma omp taskloop simd
    for (int i = n; i > 0; --i)
      ;
  }
};

S s(1);
