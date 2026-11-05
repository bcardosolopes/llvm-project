// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void plain(int *p) {
#pragma omp target teams distribute map(tofrom : p[0:4])
  for (int i = 0; i < 4; ++i)
    p[i]++;
}

void chunked(int *p, int chunk) {
#pragma omp target teams distribute map(tofrom : p[0:4])                    \
    dist_schedule(static, chunk)
  for (int i = 0; i < 4; ++i)
    p[i]++;
}

// CIR: omp.private {type = firstprivate} @__omp_firstprivate_.capture_expr.{{.*}} : !s32i copy

// CIR-LABEL: cir.func{{.*}} @plain(
// CIR: %[[PLAIN_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef)
// CIR: omp.target host_eval(%{{.*}} -> %[[PLAIN_LB:arg[0-9]+]], %{{.*}} -> %[[PLAIN_UB:arg[0-9]+]], %{{.*}} -> %[[PLAIN_STEP:arg[0-9]+]] : i32, i32, i32) map_entries(%[[PLAIN_MAP]] -> %{{.*}} : !cir.ptr<!s32i>) {
// CIR: omp.teams {
// CIR-NOT: cir.scope
// CIR: omp.distribute {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%[[PLAIN_LB]]) to (%[[PLAIN_UB]]) step (%[[PLAIN_STEP]]) {

// CIR-LABEL: cir.func{{.*}} @chunked(
// CIR: %[[CHUNK_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(implicit) capture(ByCopy) -> !cir.ptr<!s32i>
// CIR: omp.target host_eval(%{{.*}} -> %[[CHUNK_LB:arg[0-9]+]], %{{.*}} -> %[[CHUNK_UB:arg[0-9]+]], %{{.*}} -> %[[CHUNK_STEP:arg[0-9]+]] : i32, i32, i32) map_entries(%[[CHUNK_MAP]] -> %{{.*}}, %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) private(@__omp_firstprivate_.capture_expr.{{.*}} %{{.*}} -> %[[CHUNK_ARG:arg[0-9]+]] [map_idx=0] : !cir.ptr<!s32i>) {
// CIR: %[[CHUNK_VAL:.*]] = cir.load align(4) %[[CHUNK_ARG]] : !cir.ptr<!s32i>, !s32i
// CIR: %[[CHUNK_I32:.*]] = builtin.unrealized_conversion_cast %[[CHUNK_VAL]] : !s32i to i32
// CIR: omp.teams {
// CIR-NOT: cir.scope
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%[[CHUNK_I32]] : i32) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%[[CHUNK_LB]]) to (%[[CHUNK_UB]]) step (%[[CHUNK_STEP]]) {

// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 35, i64 16384]
// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [3 x i64] [i64 800, i64 35, i64 16384]

// LLVM-LABEL: define internal void @{{.*}}plain{{.*}}..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 {{[0-9]+}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)

// LLVM-LABEL: define internal void @{{.*}}chunked{{.*}}..omp_par(
// LLVM-SAME: ptr %{{.*}}, ptr %{{.*}}, i32 [[CHUNK:%[0-9]+]], ptr %{{.*}})
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 {{[0-9]+}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 [[CHUNK]])

// OGCG-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 35, i64 16384]
// OGCG-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [3 x i64] [i64 35, i64 16384, i64 800]

// OGCG-LABEL: define dso_local void @plain(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_plain_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_plain_{{.*}}.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @chunked(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_chunked_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_chunked_{{.*}}.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
