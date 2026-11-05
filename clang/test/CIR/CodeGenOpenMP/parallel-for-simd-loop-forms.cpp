// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=45 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=45 -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=45 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long step();
double *g_ptr;

void parallel_for_simd_nonunit_stride(float *a, float *b, float *c, float *d) {
#pragma omp parallel for simd
  for (int i = 3; i < 32; i += 5)
    a[i] = b[i] * c[i] * d[i];
}

void parallel_for_simd_descending_linear(int *p) {
  long long k = step();
#pragma omp parallel for simd linear(k : 3) schedule(dynamic)
  for (int i = 10; i > 1; --i)
    p[0] += i + k;
}

void parallel_for_simd_descending_nonunit_linear(int *p) {
  int a = 0;
  int lin = 12;
#pragma omp parallel for simd linear(lin, a : step()) linear(g_ptr)
  for (unsigned long long it = 2000; it >= 600; it -= 400) {
    *g_ptr++ = 0.0;
    p[0] += a + lin + (int)it;
  }
}

void parallel_for_simd_lastprivate(int *out) {
  int last = -1;
#pragma omp parallel for simd lastprivate(last)
  for (long long i = -10; i < 10; i += 3)
    last = (int)i;
  out[0] = last;
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_for_simd_nonunit_stridePfS_S_S_(
// CIR: omp.parallel
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["i"]
// CIR: omp.wsloop nowait
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_for_simd_descending_linearPi(
// CIR: omp.parallel
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["i"]
// CIR: omp.wsloop linear(
// CIR: nowait schedule(dynamic) {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_for_simd_descending_nonunit_linearPi(
// CIR: omp.parallel
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["it"]
// CIR: omp.wsloop linear(
// CIR: nowait {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i64 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_for_simd_lastprivatePi(
// CIR: omp.parallel
// CIR: omp.wsloop nowait private(
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i64 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {
// CIR: cir.if %{{.*}} {

// OGCG-LABEL: define dso_local void @_Z32parallel_for_simd_nonunit_stridePfS_S_S_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z32parallel_for_simd_nonunit_stridePfS_S_S_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z35parallel_for_simd_descending_linearPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z35parallel_for_simd_descending_linearPi.omp_outlined(
// OGCG: call void @__kmpc_dispatch_init_4(

// OGCG-LABEL: define dso_local void @_Z43parallel_for_simd_descending_nonunit_linearPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z43parallel_for_simd_descending_nonunit_linearPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_8u(

// OGCG-LABEL: define dso_local void @_Z29parallel_for_simd_lastprivatePi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z29parallel_for_simd_lastprivatePi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_8(
