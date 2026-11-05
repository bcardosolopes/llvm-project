// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sink(int);

void nested_capture(int n, int lb, int ub, int chunk, int cond) {
#pragma omp target teams
  {
#pragma omp distribute parallel for dist_schedule(static, chunk) if(cond) firstprivate(n)
    for (int i = lb; i < ub; ++i)
      sink(i + n);
  }
}

void nested_schedule_chunk(int n, int lb, int ub, int chunk, int cond) {
#pragma omp target teams
  {
#pragma omp distribute parallel for schedule(static, chunk) if(cond) firstprivate(n)
    for (int i = lb; i < ub; ++i)
      sink(i + n);
  }
}

void nested_dynamic_schedule_chunk(int n, int lb, int ub, int chunk) {
#pragma omp target teams
  {
#pragma omp distribute parallel for schedule(dynamic, chunk) firstprivate(n)
    for (int i = lb; i < ub; ++i)
      sink(i + n);
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z14nested_captureiiiii(
// CIR: omp.target map_entries(
// CIR-SAME: private(
// CIR: omp.teams private(
// CIR: %[[CONDLOAD:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[CONDBOOL:.*]] = cir.cast int_to_bool %[[CONDLOAD]] : !s32i -> !cir.bool
// CIR: %[[COND:.*]] = builtin.unrealized_conversion_cast %[[CONDBOOL]] : !cir.bool to i1
// CIR: omp.parallel if(%[[COND]])
// CIR: %[[LBLOAD:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[LB:.*]] = builtin.unrealized_conversion_cast %[[LBLOAD]] : !s32i to i32
// CIR: %[[UBLOAD:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[UB:.*]] = builtin.unrealized_conversion_cast %[[UBLOAD]] : !s32i to i32
// CIR: %[[STEPLOAD:.*]] = cir.const #cir.int<1> : !u32i
// CIR: %[[STEP:.*]] = builtin.unrealized_conversion_cast %[[STEPLOAD]] : !u32i to i32
// CIR: %[[CHUNKLOAD:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[CHUNK:.*]] = builtin.unrealized_conversion_cast %[[CHUNKLOAD]] : !s32i to i32
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%[[CHUNK]] : i32) {
// CIR: omp.wsloop nowait {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%[[LB]]) to (%[[UB]]) step (%[[STEP]]) {
// CIR: cir.call @_Z4sinki

// CIR-LABEL: cir.func{{.*}} @_Z21nested_schedule_chunkiiiii(
// CIR: omp.target map_entries(
// CIR-SAME: private(
// CIR: omp.teams private(
// CIR: %[[CHUNKTMP:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, [".capture_expr."] {alignment = 4 : i64}
// CIR: %[[CHUNKLOAD0:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: cir.store align(4) %[[CHUNKLOAD0]], %[[CHUNKTMP]] : !s32i, !cir.ptr<!s32i>
// CIR: %[[CHUNKLOAD1:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[CHUNK:.*]] = builtin.unrealized_conversion_cast %[[CHUNKLOAD1]] : !s32i to i32
// CIR: %[[CONDLOAD:.*]] = cir.load align(4) %arg{{[0-9]+}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[CONDBOOL:.*]] = cir.cast int_to_bool %[[CONDLOAD]] : !s32i -> !cir.bool
// CIR: %[[COND:.*]] = builtin.unrealized_conversion_cast %[[CONDBOOL]] : !cir.bool to i1
// CIR: omp.parallel if(%[[COND]]) private(
// CIR-SAME: @__omp_firstprivate_chunk_{{[0-9]+}}_55.2
// CIR: omp.distribute {
// CIR: omp.wsloop nowait schedule(static = %[[CHUNK]] : i32) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {
// CIR: cir.call @_Z4sinki

// OGCG-LABEL: @_Z14nested_captureiiiii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z21nested_schedule_chunkiiiii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z29nested_dynamic_schedule_chunkiiii(
// OGCG: call i32 @__tgt_target_kernel(

