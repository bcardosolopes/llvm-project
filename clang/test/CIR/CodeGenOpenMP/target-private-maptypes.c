// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int read(void);

void target_firstprivate(void) {
  int a = read();
#pragma omp target firstprivate(a)
  a += 8;
}

void target_teams_firstprivate(void) {
  int a = read();
#pragma omp target teams firstprivate(a)
  a += 16;
}

// CIR: omp.private {type = firstprivate} @__omp_firstprivate_a_

// CIR-LABEL: cir.func{{.*}} @target_firstprivate
// CIR: %[[MAP0:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(none) capture(ByCopy) -> !cir.ptr<!s32i>
// CIR: omp.target map_entries(%[[MAP0]] -> %{{.*}} : !cir.ptr<!s32i>) private(@__omp_firstprivate_a_{{[0-9_\.]+}}

// CIR-LABEL: cir.func{{.*}} @target_teams_firstprivate
// CIR: %[[MAP1:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(none) capture(ByCopy) -> !cir.ptr<!s32i>
// CIR: omp.target map_entries(%[[MAP1]] -> %{{.*}} : !cir.ptr<!s32i>) private(@__omp_firstprivate_a_{{[0-9_\.]+}}
// CIR: omp.teams

// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 288]
// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 288]
// LLVM-NOT: [1 x i64] [i64 800]

// OGCG-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 288]
// OGCG-NOT: [1 x i64] [i64 800]

// OGCG-LABEL: define dso_local void @target_firstprivate(
// OGCG: call i32 @read()
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_firstprivate_{{.*}}(

// OGCG-LABEL: define dso_local void @target_teams_firstprivate(
// OGCG: call i32 @read()
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_firstprivate_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
