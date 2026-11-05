// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long get_val();
double *g_ptr;

void for_simd_nonunit_stride(float *a, float *b, float *c, float *d) {
#pragma omp for simd
  for (int i = 3; i < 32; i += 5)
    a[i] = b[i] * c[i] * d[i];
}

void for_simd_descending_linear(int *p) {
  long long k = get_val();
#pragma omp for simd linear(k : 3) schedule(simd, nonmonotonic : dynamic)
  for (int i = 10; i > 1; --i)
    p[0] += i + k;
}

void for_simd_descending_nonunit_linear(int *p) {
  int lin = 12;
#pragma omp for simd linear(lin : get_val()), linear(g_ptr)
  for (unsigned long long it = 2000; it >= 600; it -= 400)
    *g_ptr++ = (double)(lin + (int)it);
}

// CIR-LABEL: cir.func{{.*}} @_Z23for_simd_nonunit_stridePfS_S_S_(
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["i"]
// CIR: omp.wsloop {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z26for_simd_descending_linearPi(
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["i"]
// CIR: omp.wsloop linear(
// CIR: schedule(dynamic, nonmonotonic, simd) {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}for_simd_descending_nonunit_linearPi(
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["it"]
// CIR: omp.wsloop linear(
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i64 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// OGCG-LABEL: define {{.*}} @_Z23for_simd_nonunit_stridePfS_S_S_(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z26for_simd_descending_linearPi(
// OGCG: call void @__kmpc_dispatch_init_4(
// OGCG: call void @__kmpc_dispatch_deinit(

// OGCG-LABEL: define {{.*}} @_Z34for_simd_descending_nonunit_linearPi(
// OGCG: call void @__kmpc_for_static_init_8u(
// OGCG: call void @__kmpc_for_static_fini(
