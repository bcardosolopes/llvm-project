// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void body();

void basic(int *p, int n, int teams, int threads, int chunk) {
#pragma omp target teams distribute parallel for map(tofrom : p[0:1]) \
    if(target : n) if(parallel : n + 1) num_teams(teams) \
    thread_limit(threads) num_threads(n) firstprivate(n) \
    dist_schedule(static, chunk) schedule(static) proc_bind(close)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void sched_chunk(int *p, int n, int chunk) {
#pragma omp target teams distribute parallel for map(tofrom : p[0:1]) \
    firstprivate(n) schedule(static, chunk)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

// CIR-LABEL: cir.func{{.*}} @_Z5basicPiiiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32) host_eval(%{{.*}} -> %[[TEAMS:arg[0-9]+]], %{{.*}} -> %[[THREAD_LIMIT:arg[0-9]+]], %{{.*}} -> %[[LB:arg[0-9]+]], %{{.*}} -> %[[UB:arg[0-9]+]], %{{.*}} -> %[[STEP:arg[0-9]+]], %{{.*}} -> %[[PAR_IF:arg[0-9]+]], %{{.*}} -> %[[PAR_NT:arg[0-9]+]] : i32, i32, i32, i32, i32, i1, i32)
// CIR: omp.teams
// CIR-SAME: num_teams( to %[[TEAMS]] : i32)
// CIR-SAME: thread_limit(%[[THREAD_LIMIT]] : i32)
// CIR-NOT: cir.scope
// CIR: omp.parallel if(%[[PAR_IF]]) num_threads(%[[PAR_NT]] : i32) proc_bind(close)
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.wsloop nowait schedule(static) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%[[LB]]) to (%[[UB]]) step (%[[STEP]]) {

// CIR-LABEL: cir.func{{.*}} @_Z11sched_chunkPiii(
// CIR: omp.target
// CIR-SAME: private(
// CIR: omp.teams
// CIR: %[[CHUNKTMP:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, [".capture_expr."] {alignment = 4 : i64}
// CIR: %[[CHUNKLOAD0:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: cir.store align(4) %[[CHUNKLOAD0]], %[[CHUNKTMP]] : !s32i, !cir.ptr<!s32i>
// CIR: %[[CHUNKLOAD1:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[CHUNK:.*]] = builtin.unrealized_conversion_cast %[[CHUNKLOAD1]] : !s32i to i32
// CIR: omp.parallel
// CIR-SAME: @__omp_firstprivate_chunk_{{[0-9]+}}_37.1
// CIR: omp.distribute {
// CIR: omp.wsloop nowait schedule(static = %[[CHUNK]] : i32) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z5basicPiiiii_l{{[0-9]+}}(
// LLVM: call void @__kmpc_push_num_teams{{(_51)?}}(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z5basicPiiiii_l{{[0-9]+}}..omp_par.
// LLVM: call void @__kmpc_push_num_threads(
// LLVM: call void @__kmpc_push_proc_bind(
// LLVM: call void @__kmpc_fork_call_if(
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z5basicPiiiii_l{{[0-9]+}}..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_init_4(

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z11sched_chunkPiii_l{{[0-9]+}}..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: @_Z5basicPiiiii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z11sched_chunkPiii(
// OGCG: call i32 @__tgt_target_kernel(
