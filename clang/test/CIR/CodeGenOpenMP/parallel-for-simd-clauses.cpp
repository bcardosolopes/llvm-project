// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=TARGET
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long step();
double *g_ptr;

void parallel_for_simd_linear(int *p) {
  long long k = step();
#pragma omp parallel for simd linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void parallel_for_simd_nontemporal(int *p, int *q) {
#pragma omp parallel for simd nontemporal(p, q)
  for (int i = 0; i < 4; ++i)
    p[0] += q[0];
}

void parallel_for_simd_clause_bundle(float *p, int n) {
#pragma omp parallel for simd if(parallel : n) if(simd : n) simdlen(4) \
    safelen(8) aligned(p : 16) order(concurrent)
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

void parallel_for_simd_pointer_linear(int *p) {
#pragma omp parallel for simd linear(g_ptr)
  for (int i = 0; i < 4; ++i)
    p[0] += i;
}

void target_parallel_for_simd_linear(int *p) {
  long long k = step();
#pragma omp target parallel for simd map(tofrom : p[0:1]) linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void target_parallel_for_simd_nontemporal(int *p, int *q) {
#pragma omp target parallel for simd map(tofrom : p[0:1], q[0:1]) \
    nontemporal(p, q)
  for (int i = 0; i < 4; ++i)
    p[0] += q[0];
}

void target_parallel_for_simd_pointer_linear(int *p) {
#pragma omp target parallel for simd map(tofrom : p[0:1]) linear(g_ptr)
  for (int i = 0; i < 4; ++i)
    p[0] += i;
}

// HOST-LABEL: cir.func{{.*}} @_Z24parallel_for_simd_linearPi(
// HOST: omp.parallel
// HOST: omp.wsloop linear(
// HOST: nowait {
// HOST: omp.simd {

// HOST-LABEL: cir.func{{.*}} @_Z29parallel_for_simd_nontemporalPiS_(
// HOST: omp.parallel
// HOST: omp.wsloop nowait
// HOST: omp.simd nontemporal(
// HOST: cir.load nontemporal

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_for_simd_clause_bundlePfi(
// HOST: omp.parallel if(%{{.*}})
// HOST: omp.wsloop nowait
// HOST: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) order(concurrent) safelen(8) simdlen(4) {

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_for_simd_pointer_linearPi(
// HOST: omp.parallel
// HOST: omp.wsloop linear(
// HOST: nowait {
// HOST: omp.simd {

// TARGET-LABEL: cir.func{{.*}} @_Z31target_parallel_for_simd_linearPi(
// TARGET: omp.target
// TARGET: omp.parallel
// TARGET: omp.wsloop linear(
// TARGET: nowait {
// TARGET: omp.simd {

// TARGET-LABEL: cir.func{{.*}} @_Z36target_parallel_for_simd_nontemporalPiS_(
// TARGET: omp.target
// TARGET: omp.parallel
// TARGET: omp.wsloop nowait
// TARGET: omp.simd nontemporal(
// TARGET: cir.load nontemporal

// TARGET-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_pointer_linearPi(
// TARGET: omp.target
// TARGET: omp.parallel
// TARGET: omp.wsloop linear(
// TARGET: nowait {
// TARGET: omp.simd {

// OGCG-LABEL: define dso_local void @_Z24parallel_for_simd_linearPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z24parallel_for_simd_linearPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z29parallel_for_simd_nontemporalPiS_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z29parallel_for_simd_nontemporalPiS_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z31parallel_for_simd_clause_bundlePfi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z31parallel_for_simd_clause_bundlePfi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z32parallel_for_simd_pointer_linearPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z32parallel_for_simd_pointer_linearPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
