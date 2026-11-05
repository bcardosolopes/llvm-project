// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void bundle(float *p, float *q, int n, int teams, int threads, int chunk) {
  int i;
#pragma omp target teams distribute simd                                         \
    map(tofrom : p[0:8], q[0:8]) if(target : n) num_teams(teams)                 \
        thread_limit(threads) firstprivate(n) dist_schedule(static, chunk)       \
            order(concurrent) simdlen(4) safelen(8) aligned(p : 16)              \
                nontemporal(p, q) if(simd : n + 1) linear(i)
  for (i = 0; i < 8; ++i)
    p[i] += q[i] + n;
}

void generated(float *p, float *q, int teams) {
#pragma omp target teams distribute simd map(tofrom : p[0:16], q[0:16])          \
    num_teams(teams) dist_schedule(static) if(simd : teams)
  for (int i = 8; i > 0; i += -1)
    p[i] += q[i];
}

// CIR-LABEL: cir.func{{.*}} @_Z6bundlePfS_iiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32) host_eval(%{{.*}} -> %[[TEAMS:arg[0-9]+]], %{{.*}} -> %[[THREADS:arg[0-9]+]], %{{.*}} -> %[[LB:arg[0-9]+]], %{{.*}} -> %[[UB:arg[0-9]+]], %{{.*}} -> %[[STEP:arg[0-9]+]] : i32, i32, i32, i32, i32)
// CIR: omp.teams
// CIR-SAME: num_teams( to %[[TEAMS]] : i32)
// CIR-SAME: thread_limit(%[[THREADS]] : i32)
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) nontemporal(%{{.*}}, %{{.*}}) order(concurrent) safelen(8) simdlen(4) {
// CIR-NOT: linear(
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%[[LB]]) to (%[[UB]]) step (%[[STEP]]) {

// CIR-LABEL: cir.func{{.*}} @_Z9generatedPfS_i(
// CIR: omp.target host_eval(%{{.*}} -> %[[GEN_TEAMS:arg[0-9]+]] : i32)
// CIR: omp.teams
// CIR-SAME: num_teams( to %[[GEN_TEAMS]] : i32)
// CIR: omp.distribute dist_schedule_static {
// CIR: omp.simd if(%{{.*}}) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z6bundlePfS_iiii
// LLVM: call void @__kmpc_push_num_teams{{(_51)?}}(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z6bundlePfS_iiii{{.*}}omp_par(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: !nontemporal !{{.*}}
// LLVM: !llvm.access.group !{{.*}}

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z9generatedPfS_i
// LLVM: call void @__kmpc_push_num_teams{{(_51)?}}(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z9generatedPfS_i{{.*}}omp_par
// LLVM: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: @_Z6bundlePfS_iiii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z9generatedPfS_i(
// OGCG: call i32 @__tgt_target_kernel(
