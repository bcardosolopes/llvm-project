// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void distribute_nonunit_positive(float *a, float *b, float *c, float *d) {
#pragma omp teams
  {
#pragma omp distribute
    for (int i = 33; i < 32000000; i += 7)
      a[i] = b[i] * c[i] * d[i];
  }
}

void distribute_descending(float *a, float *b, float *c, float *d) {
#pragma omp teams
  {
#pragma omp distribute dist_schedule(static)
    for (int i = 32000000; i > 33; i += -7)
      a[i] = b[i] * c[i] * d[i];
  }
}

void distribute_unsigned_step(float *a, float *b, float *c, float *d) {
#pragma omp teams
  {
#pragma omp distribute dist_schedule(static, 5)
    for (unsigned i = 131071; i <= 2147483647; i += 127)
      a[i] = b[i] * c[i] * d[i];
  }
}

void distribute_mixed_bounds(char a) {
#pragma omp teams
  {
#pragma omp distribute
    for (char i = a; i < 10; ++i)
      ;
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_nonunit_positivePfS_S_S_(
// CIR: omp.teams {
// CIR: omp.distribute {
// CIR: omp.loop_nest (%{{.*}}) : {{.*}} = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_descendingPfS_S_S_(
// CIR: omp.teams {
// CIR: omp.distribute dist_schedule_static {
// CIR: omp.loop_nest (%{{.*}}) : {{.*}} = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_unsigned_stepPfS_S_S_(
// CIR: omp.teams {
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.loop_nest (%{{.*}}) : {{.*}} = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_mixed_boundsc(
// CIR: omp.teams {
// CIR: omp.distribute {
// CIR: omp.loop_nest (%{{.*}}) : {{.*}} = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// OGCG-LABEL: define dso_local void @_Z27distribute_nonunit_positivePfS_S_S_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z27distribute_nonunit_positivePfS_S_S_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z21distribute_descendingPfS_S_S_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z21distribute_descendingPfS_S_S_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z24distribute_unsigned_stepPfS_S_S_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z24distribute_unsigned_stepPfS_S_S_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4u(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z23distribute_mixed_boundsc(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z23distribute_mixed_boundsc.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
