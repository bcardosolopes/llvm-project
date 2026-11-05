// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void work(void);

void host_target_teams_nowait(void) {
  // CIR-LABEL: cir.func{{.*}} @host_target_teams_nowait(
  // CIR: omp.target nowait {
  // CIR: omp.teams {
  // CIR: cir.call @work
#pragma omp target teams nowait
  work();
}

void host_target_teams_depend(int *dep) {
  // CIR-LABEL: cir.func{{.*}} @host_target_teams_depend(
  // CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64)) {
  // CIR: omp.teams {
  // CIR: cir.call @work
#pragma omp target teams depend(inout : dep[0])
  work();
}

// LLVM-LABEL: define dso_local void @host_target_teams_nowait(
// LLVM: call ptr @__kmpc_omp_target_task_alloc(ptr @{{.*}}, i32 %{{.*}}, i32 0, i32 20, i32 0, ptr @.omp_target_task_proxy_func, i64 -1)
// LLVM: call i32 @__kmpc_omp_task(

// LLVM-LABEL: define dso_local void @host_target_teams_depend(
// LLVM: call ptr @__kmpc_omp_task_alloc(ptr @{{.*}}, i32 %{{.*}}, i32 0, i32 20, i32 0, ptr @.omp_target_task_proxy_func.1)
// LLVM: call void @__kmpc_omp_taskwait_deps_51(
// LLVM: call void @__kmpc_omp_task_begin_if0(
// LLVM: call void @__kmpc_omp_task_complete_if0(

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}host_target_teams_nowait{{.*}}(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}host_target_teams_depend{{.*}}(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @host_target_teams_nowait(
// OGCG: call ptr @__kmpc_omp_target_task_alloc(
// OGCG: call i32 @__kmpc_omp_task(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_host_target_teams_nowait_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @host_target_teams_depend(
// OGCG: call ptr @__kmpc_omp_task_alloc(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(
// OGCG: call void @__kmpc_omp_task_begin_if0(
// OGCG: call void @__kmpc_omp_task_complete_if0(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_host_target_teams_depend_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
