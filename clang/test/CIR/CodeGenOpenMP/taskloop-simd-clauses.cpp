// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long step();

void taskloop_simd_linear(int *p) {
  long long k = step();
#pragma omp taskloop simd linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void taskloop_simd_nontemporal(int *p, int *q) {
#pragma omp taskloop simd nontemporal(p, q)
  for (int i = 0; i < 4; ++i)
    p[0] += q[0];
}

void taskloop_simd_clause_bundle(float *p, int n) {
#pragma omp taskloop simd if(simd : n) simdlen(4) safelen(8) aligned(p : 16) \
    order(concurrent)
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

void taskloop_simd_task_clause_bundle(float *p, int n) {
#pragma omp taskloop simd if(taskloop : n) if(simd : n) final(n) mergeable \
    priority(n) untied
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_simd_linearPi(
// CIR: omp.taskloop
// CIR: omp.simd {{.*}}linear(

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_simd_nontemporalPiS_(
// CIR: omp.taskloop
// CIR: omp.simd nontemporal(
// CIR: cir.load nontemporal

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_simd_clause_bundlePfi(
// CIR: omp.taskloop
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) order(concurrent) safelen(8) simdlen(4) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_simd_task_clause_bundlePfi(
// CIR: omp.taskloop final(%{{.*}}) if(%{{.*}}) mergeable priority(%{{.*}}) untied {{.*}}{
// CIR: omp.simd if(%{{.*}}) {

// OGCG-LABEL: define {{.*}} @_Z20taskloop_simd_linearPi(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z25taskloop_simd_nontemporalPiS_(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z27taskloop_simd_clause_bundlePfi(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z32taskloop_simd_task_clause_bundlePfi(
// OGCG: call void @__kmpc_taskloop(
