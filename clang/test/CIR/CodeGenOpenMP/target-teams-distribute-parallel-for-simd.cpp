// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void body();

void bundle(float *p, float *q, int n, int teams, int threads, int chunk) {
  int i;
#pragma omp target teams distribute parallel for simd                         \
    map(tofrom : p[0:8], q[0:8]) if(target : n) if(parallel : n + 1)         \
        if(simd : n + 2) num_teams(teams) thread_limit(threads)              \
            num_threads(n) firstprivate(n) dist_schedule(static, chunk)       \
                schedule(static) proc_bind(close) order(concurrent)           \
                    simdlen(4) safelen(8) aligned(p : 16)                     \
                        nontemporal(p, q) linear(i)
  for (i = 0; i < 8; ++i)
    p[i] += q[i] + n;
}

// CIR-LABEL: cir.func{{.*}} @_Z6bundlePfS_iiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32) host_eval(%{{.*}} -> %[[TEAMS:arg[0-9]+]], %{{.*}} -> %[[THREADS:arg[0-9]+]], %{{.*}} -> %[[LB:arg[0-9]+]], %{{.*}} -> %[[UB:arg[0-9]+]], %{{.*}} -> %[[STEP:arg[0-9]+]], %{{.*}} -> %[[PAR_IF:arg[0-9]+]], %{{.*}} -> %[[PAR_NT:arg[0-9]+]] : i32, i32, i32, i32, i32, i1, i32)
// CIR: omp.teams
// CIR-SAME: num_teams( to %[[TEAMS]] : i32)
// CIR-SAME: thread_limit(%[[THREADS]] : i32)
// CIR: omp.parallel if(%[[PAR_IF]]) num_threads(%[[PAR_NT]] : i32) proc_bind(close)
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.wsloop linear(%{{.*}} = %{{.*}} : !cir.ptr<!s32i>) nowait schedule(static) {
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) nontemporal(%{{.*}}, %{{.*}}) order(concurrent) safelen(8) simdlen(4) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%[[LB]]) to (%[[UB]]) step (%[[STEP]]) {

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z6bundlePfS_iiii_l{{[0-9]+}}(
// LLVM: call void @__kmpc_push_num_teams{{(_51)?}}(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z6bundlePfS_iiii_l{{[0-9]+}}..omp_par.1(
// LLVM: call void @__kmpc_push_num_threads(
// LLVM: call void @__kmpc_push_proc_bind(
// LLVM: call void @__kmpc_fork_call_if(
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z6bundlePfS_iiii_l{{[0-9]+}}..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: !nontemporal !{{.*}}
// LLVM: !llvm.access.group !{{.*}}

// OGCG-LABEL: @_Z6bundlePfS_iiii(
// OGCG: call i32 @__tgt_target_kernel(
