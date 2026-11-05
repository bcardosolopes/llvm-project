// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

template <class T, unsigned K> T tfoo(T a) { return a + K; }

template <typename T, unsigned N>
int templ1(T a, T *z) {
#pragma omp for simd collapse(N) schedule(simd: static, N)
  for (int i = 0; i < N * 2; i++) {
    for (long long j = 0; j < (N + N + N + N); j += 2)
      z[i + j] = a + tfoo<T, N>(i + j);
  }
  return 0;
}

template int templ1<float, 2>(float, float *);

// CIR-LABEL: cir.func{{.*}} @_Z6templ1IfLj2EEiT_PS0_(
// CIR: omp.wsloop
// CIR: omp.simd
// CIR: omp.loop_nest

void collapsed_four(int *out) {
  unsigned j;
  short l;
#pragma omp for simd collapse(4)
  for (int i = 1; i < 3; i++)
    for (j = 2u; j < 5u; j++)
      for (int k = 3; k <= 6; k++)
        for (l = 4; l < 9; ++l)
          out[(i - 1) * 60 + (int)((j - 2u) * 20) + (k - 3) * 5 + (l - 4)] =
              i + (int)j + k + l;
}

// CIR-LABEL: cir.func{{.*}} @_Z14collapsed_fourPi(
// CIR: omp.wsloop
// CIR: omp.simd
// CIR: omp.loop_nest

float globalfloat;
int foo();

void collapsed_private(int *out) {
  int localint = 0;
#pragma omp for simd collapse(2) private(globalfloat, localint)
  for (int i = 1; i < 3; i++)
    for (int j = 0; j < foo(); j++)
      out[i + j] = j;
}

// CIR-LABEL: cir.func{{.*}} @_Z17collapsed_privatePi(
// CIR: omp.wsloop
// CIR: omp.simd
// CIR: omp.loop_nest

// OGCG-LABEL: define {{.*}} @_Z6templ1IfLj2EEiT_PS0_(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z14collapsed_fourPi(
// OGCG: call void @__kmpc_for_static_init_4u(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z17collapsed_privatePi(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(
