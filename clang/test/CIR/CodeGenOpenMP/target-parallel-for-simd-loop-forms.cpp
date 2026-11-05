// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long get_val();

void target_parallel_for_simd_descending_linear(int *p) {
  long long k = get_val();
#pragma omp target parallel for simd if(target : 0) linear(k : 3) \
    schedule(dynamic) map(tofrom : p[0:1])
  for (int i = 10; i > 1; --i)
    p[0] += i + k;
}

void target_parallel_for_simd_descending_nonunit_linear(int *p) {
  int a = 0;
  int lin = 12;
#pragma omp target parallel for simd if(target : 1) linear(lin, a : get_val()) \
    map(tofrom : p[0:1])
  for (unsigned long long it = 2000; it >= 600; it -= 400)
    p[0] += a + lin + (int)it;
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_descending_linearPi(
// CIR: omp.target if(%{{.*}})
// CIR: omp.parallel
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["i"]
// CIR: omp.wsloop linear(
// CIR: nowait schedule(dynamic) {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_for_simd_descending_nonunit_linearPi(
// CIR: omp.target if(%{{.*}})
// CIR: omp.parallel
// CIR: cir.alloca {{.*}} [".omp.iv"]
// CIR: cir.alloca {{.*}} ["it"]
// CIR: omp.wsloop linear(
// CIR: nowait {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i64 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// OGCG-LABEL: @_Z42target_parallel_for_simd_descending_linearPi(
// OGCG: call void @__omp_offloading_{{.*}}__Z42target_parallel_for_simd_descending_linearPi

// OGCG-LABEL: @_Z50target_parallel_for_simd_descending_nonunit_linearPi(
// OGCG: call i32 @__tgt_target_kernel(
