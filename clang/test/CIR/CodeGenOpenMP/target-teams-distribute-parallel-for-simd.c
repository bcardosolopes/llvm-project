// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fopenmp -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fopenmp -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

int A[40000];

long target_teams_distribute_parallel_for_simd(void) {
  long s = 0;
#pragma omp target teams distribute parallel for simd num_teams(4) map(tofrom: s) map(to: A)
  for (int i = 0; i < 40000; ++i)
    s += A[i];
  return s;
}

// The host lowering of this composite builds
// omp.teams > omp.parallel > omp.distribute > omp.wsloop > omp.simd >
// omp.loop_nest. The loop bounds feed the omp.loop_nest nested inside all of
// those regions, so their defining ops must be emitted before omp.teams;
// otherwise the module fails verification with "operand does not dominate this
// use". Capture the bound defs before omp.teams and match them on the loop_nest.

// CIR-LABEL: cir.func{{.*}} @target_teams_distribute_parallel_for_simd(
// CIR:         %[[LB:.*]] = builtin.unrealized_conversion_cast %{{.*}} : !s32i to i32
// CIR:         %[[UB:.*]] = builtin.unrealized_conversion_cast %{{.*}} : !s32i to i32
// CIR:         %[[STEP:.*]] = builtin.unrealized_conversion_cast %{{.*}} : !u32i to i32
// CIR:         omp.teams num_teams(
// CIR:           omp.parallel
// CIR:             omp.distribute {
// CIR:               omp.wsloop nowait {
// CIR:                 omp.simd {
// CIR:                   omp.loop_nest (%{{.*}}) : i32 = (%[[LB]]) to (%[[UB]]) step (%[[STEP]]) {

// A composite 'distribute parallel for' with the default (static) distribute
// schedule emits two static-init calls: the worksharing 'for' chunking across
// threads (schedule 34) and the 'distribute' chunking across teams (schedule
// 92).
// LLVM-LABEL: define dso_local i64 @target_teams_distribute_parallel_for_simd(
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @target_teams_distribute_parallel_for_simd..omp_par(
// LLVM:         call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 34,
// LLVM:         call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 92,

// OGCG-LABEL: define dso_local i64 @target_teams_distribute_parallel_for_simd(
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG:         call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 92,
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG:         call void @__kmpc_for_static_init_4(ptr {{.*}}, i32 {{.*}}, i32 34,
