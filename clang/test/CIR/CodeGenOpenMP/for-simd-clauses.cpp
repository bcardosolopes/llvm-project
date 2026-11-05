// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long step();

void for_simd_linear(int *p) {
  long long k = step();
#pragma omp for simd linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void for_simd_nontemporal(int *p, int *q) {
#pragma omp for simd nontemporal(p, q)
  for (int i = 0; i < 4; ++i)
    p[0] += q[0];
}

void for_simd_clause_bundle(float *p, int n) {
#pragma omp for simd if(simd : n) simdlen(4) safelen(8) aligned(p : 16) \
    order(concurrent)
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

// CIR-LABEL: cir.func{{.*}} @_Z15for_simd_linearPi(
// CIR: omp.wsloop linear(
// CIR: omp.simd {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}for_simd_nontemporalPiS_(
// CIR: omp.wsloop {
// CIR: omp.simd nontemporal(
// CIR: cir.load nontemporal

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}for_simd_clause_bundlePfi(
// CIR: omp.wsloop {
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) order(concurrent) safelen(8) simdlen(4) {

// OGCG-LABEL: define {{.*}} @_Z15for_simd_linearPi(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}for_simd_nontemporalPiS_(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}for_simd_clause_bundlePfi(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
