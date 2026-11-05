// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void for_simd_lastprivate() {
  int A = -1;
#pragma omp parallel
  {
#pragma omp for simd lastprivate(A)
    for (long long i = -10; i < 10; i += 3)
      A = (int)i;
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z20for_simd_lastprivatev(
// CIR: omp.parallel
// CIR: omp.wsloop
// CIR: omp.simd
// CIR: cir.if

void for_simd_private(int *out) {
#pragma omp parallel
  {
    int tmp = 0;
#pragma omp for simd private(tmp)
    for (int i = 0; i < 4; ++i) {
      tmp = i;
      out[i] = tmp;
    }
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z16for_simd_privatePi(
// CIR: omp.parallel
// CIR: omp.wsloop
// CIR: omp.simd

// OGCG-LABEL: define {{.*}} @_Z20for_simd_lastprivatev(
// OGCG: call void {{.*}} @__kmpc_fork_call(

// OGCG-LABEL: define internal {{.*}} @_Z20for_simd_lastprivatev.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_8(

// OGCG-LABEL: define {{.*}} @_Z16for_simd_privatePi(
// OGCG: call void {{.*}} @__kmpc_fork_call(

// OGCG-LABEL: define internal {{.*}} @_Z16for_simd_privatePi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
