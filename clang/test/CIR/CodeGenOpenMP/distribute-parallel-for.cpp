// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=c++11 -fopenmp -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=c++11 -fopenmp -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);
int n;

void distribute_parallel_for_basic(void) {
  int i;
#pragma omp teams
  {
#pragma omp distribute parallel for
    for (i = 0; i < n; ++i)
      body();
  }
}

void distribute_parallel_for_scheduled(void) {
  int i;
#pragma omp teams
  {
#pragma omp distribute parallel for dist_schedule(static) schedule(static)
    for (i = 0; i < n; ++i)
      body();
  }
}

void distribute_parallel_for_dist_chunk(int chunk) {
  int i;
#pragma omp teams
  {
#pragma omp distribute parallel for dist_schedule(static, chunk)
    for (i = 0; i < n; ++i)
      body();
  }
}

void distribute_parallel_for_cancel(void) {
  int i;
#pragma omp teams
  {
#pragma omp distribute parallel for
    for (i = 0; i < n; ++i) {
#pragma omp cancel for
      body();
    }
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z29distribute_parallel_for_basicv(
// CIR: omp.teams {
// CIR: cir.scope {
// CIR: omp.parallel
// CIR: omp.distribute {
// CIR: omp.wsloop nowait {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z33distribute_parallel_for_scheduledv(
// CIR: omp.parallel
// CIR: omp.distribute dist_schedule_static {
// CIR: omp.wsloop nowait schedule(static) {

// CIR-LABEL: cir.func{{.*}} @_Z34distribute_parallel_for_dist_chunki(
// CIR: omp.parallel
// CIR: omp.distribute dist_schedule_static
// CIR: omp.wsloop nowait {

// CIR-LABEL: cir.func{{.*}} @_Z30distribute_parallel_for_cancelv(
// CIR: omp.parallel
// CIR: omp.distribute {
// CIR: omp.wsloop {
// CIR: omp.cancel cancellation_construct_type(loop)
// CIR: cir.call @_Z4bodyv()

// LLVM-LABEL: define dso_local void @_Z29distribute_parallel_for_basicv(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z29distribute_parallel_for_basicv..omp_par.1(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM-LABEL: define internal void @_Z29distribute_parallel_for_basicv..omp_par(
// A composite 'distribute parallel for' with the default (static) distribute
// schedule must emit two static-init calls: the worksharing 'for' chunking
// across threads (schedule 34) and the 'distribute' chunking across teams
// (schedule 92). Missing the second makes every team run the whole loop.
// LLVM: call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 34,
// LLVM: call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 92,

// LLVM-LABEL: define internal void @_Z33distribute_parallel_for_scheduledv..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define internal void @_Z34distribute_parallel_for_dist_chunki..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(

// LLVM-LABEL: define internal void @_Z30distribute_parallel_for_cancelv..omp_par(
// LLVM: call void @__kmpc_for_static_fini(
// LLVM: call void @__kmpc_barrier(
// LLVM: call i32 @__kmpc_cancel(
// LLVM: call void @_Z4bodyv()

// OGCG-LABEL: define dso_local void @_Z29distribute_parallel_for_basicv(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z29distribute_parallel_for_basicv.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 92,
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z29distribute_parallel_for_basicv.omp_outlined.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 34,

// OGCG-LABEL: define dso_local void @_Z33distribute_parallel_for_scheduledv(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z34distribute_parallel_for_dist_chunki(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z30distribute_parallel_for_cancelv(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG: call i32 @__kmpc_cancel(
