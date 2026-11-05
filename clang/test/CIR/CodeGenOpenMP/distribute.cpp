// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void distribute_plain(int *p) {
#pragma omp teams
  {
#pragma omp distribute
    for (int i = 0; i < 4; ++i)
      p[i]++;
  }
}

void distribute_chunked(int *p, int chunk) {
#pragma omp teams
  {
#pragma omp distribute dist_schedule(static, chunk)
    for (int i = 0; i < 4; ++i)
      p[i]++;
  }
}

void loop_bind_teams(int *p) {
#pragma omp teams
  {
#pragma omp loop bind(teams)
    for (int i = 0; i < 4; ++i)
      p[i]++;
  }
}

void distribute_order_mod(int *p) {
#pragma omp teams
  {
#pragma omp distribute order(unconstrained:concurrent)
    for (int i = 0; i < 4; ++i)
      p[i]++;
  }
}

void distribute_order_reproducible(int *p) {
#pragma omp teams
  {
#pragma omp distribute order(reproducible:concurrent)
    for (int i = 0; i < 4; ++i)
      p[i]++;
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_plainPi(
// CIR: omp.teams {
// CIR: omp.distribute {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_chunkedPii(
// CIR: omp.teams {
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}loop_bind_teamsPi(
// CIR: omp.teams {
// CIR: omp.distribute {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_order_modPi(
// CIR: omp.teams {
// CIR: omp.distribute order(unconstrained:concurrent) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_order_reproduciblePi(
// CIR: omp.teams {
// CIR: omp.distribute order(reproducible:concurrent) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// LLVM-LABEL: define dso_local void @_Z16distribute_plainPi(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z16distribute_plainPi..omp_par(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define dso_local void @_Z18distribute_chunkedPii(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z18distribute_chunkedPii..omp_par(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(

// LLVM-LABEL: define dso_local void @_Z15loop_bind_teamsPi(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z15loop_bind_teamsPi..omp_par(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define dso_local void @_Z20distribute_order_modPi(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z20distribute_order_modPi..omp_par(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define dso_local void @_Z{{[0-9]+}}distribute_order_reproduciblePi(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z{{[0-9]+}}distribute_order_reproduciblePi..omp_par(
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z16distribute_plainPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z16distribute_plainPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z18distribute_chunkedPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z18distribute_chunkedPii.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z15loop_bind_teamsPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z15loop_bind_teamsPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z20distribute_order_modPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z20distribute_order_modPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z29distribute_order_reproduciblePi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z29distribute_order_reproduciblePi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
