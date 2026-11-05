// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void teams_loop_bind_parallel(int *p, int n, int teams, int threads) {
  int sum = 0;
#pragma omp teams loop bind(parallel) reduction(+ : sum) num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    sum += p[i] + n;
  p[0] = sum;
}

void teams_loop_bind_teams_order(int *p, int teams, int threads) {
#pragma omp teams loop bind(teams) order(concurrent) num_teams(teams) thread_limit(threads)
  for (int i = 0; i < 4; ++i)
    p[i] += i;
}

void teams_loop_collapse_lastprivate(int *out, int n, int teams, int threads) {
  int j;
#pragma omp teams loop bind(parallel) collapse(2) lastprivate(j) num_teams(teams) thread_limit(threads)
  for (int i = 0; i < n; ++i)
    for (j = 0; j < n; ++j) {
    }
  *out = j;
}

// CIR-LABEL: teams_loop_bind_parallel
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR-SAME: reduction(
// CIR-NOT: omp.parallel
// CIR: omp.distribute
// CIR-NOT: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: teams_loop_bind_teams_order
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR-NOT: omp.parallel
// CIR: omp.distribute order(concurrent)
// CIR-NOT: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: teams_loop_collapse_lastprivate
// CIR: %[[J_OUTER:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["j"]
// CIR: omp.teams
// CIR-NOT: omp.parallel
// CIR: omp.distribute private(
// CIR: omp.loop_nest
// CIR: cir.if
// CIR: cir.store align(4) %{{.*}}, %[[J_OUTER]] : !s32i, !cir.ptr<!s32i>

// OGCG-LABEL: define dso_local void @teams_loop_bind_parallel(
// OGCG: call void @__kmpc_push_num_teams
// OGCG: call void {{.*}}@__kmpc_fork_teams
// OGCG: define internal void @teams_loop_bind_parallel.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4
// OGCG: call void @__kmpc_for_static_fini
// OGCG: call i32 @__kmpc_reduce_nowait
// OGCG-LABEL: define dso_local void @teams_loop_bind_teams_order(
// OGCG: call void @__kmpc_push_num_teams
// OGCG: call void {{.*}}@__kmpc_fork_teams
// OGCG: define internal void @teams_loop_bind_teams_order.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4
// OGCG: call void @__kmpc_for_static_fini
// OGCG-LABEL: define dso_local void @teams_loop_collapse_lastprivate(
// OGCG: call void @__kmpc_push_num_teams
// OGCG: call void {{.*}}@__kmpc_fork_teams
// OGCG: define internal void @teams_loop_collapse_lastprivate.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_8
// OGCG: call void @__kmpc_for_static_fini
