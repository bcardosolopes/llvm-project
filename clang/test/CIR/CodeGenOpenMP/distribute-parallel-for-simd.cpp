// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void body();
int n;

void basic() {
  int i;
#pragma omp teams
  {
#pragma omp distribute parallel for simd
    for (i = 0; i < n; ++i)
      body();
  }
}

void bundle(float *p, float *q, int chunk) {
  int i;
#pragma omp teams
  {
#pragma omp distribute parallel for simd if(parallel : n) if(simd : n) \
    num_threads(4) proc_bind(close) dist_schedule(static, chunk) \
    schedule(static) order(concurrent) simdlen(4) safelen(8) \
    aligned(p : 16) nontemporal(p, q) linear(i)
    for (i = 0; i < 8; ++i)
      p[i] += q[i];
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z5basicv(
// CIR: omp.teams {
// CIR: cir.scope {
// CIR: omp.parallel
// CIR: omp.distribute {
// CIR: omp.wsloop nowait {
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z6bundlePfS_i(
// CIR: omp.parallel if(%{{.*}}) num_threads(%{{.*}} : i32) proc_bind(close)
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.wsloop linear(%{{.*}} = %{{.*}} : !cir.ptr<!s32i>) nowait schedule(static) {
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) nontemporal(%{{.*}}, %{{.*}}) order(concurrent) safelen(8) simdlen(4) {

// LLVM-LABEL: define dso_local void @_Z5basicv(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// LLVM-LABEL: define internal void @_Z5basicv..omp_par.1(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM-LABEL: define internal void @_Z5basicv..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(

// LLVM-LABEL: define dso_local void @_Z6bundlePfS_i(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 4, ptr @_Z6bundlePfS_i..omp_par.2
// LLVM-LABEL: define internal void @_Z6bundlePfS_i..omp_par.2(
// LLVM: call void @__kmpc_push_num_threads(ptr @{{.*}}, i32 %{{.*}}, i32 4)
// LLVM: call void @__kmpc_push_proc_bind(ptr @{{.*}}, i32 %{{.*}}, i32 3)
// LLVM: call void @__kmpc_fork_call_if(
// LLVM-LABEL: define internal void @_Z6bundlePfS_i..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 34
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 91
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}
// LLVM: load i32, ptr %.linear_result, align 4, !llvm.access.group !{{.*}}

// OGCG-LABEL: define dso_local void @_Z5basicv(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z5basicv.omp_outlined(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z6bundlePfS_i(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG: call void @__kmpc_push_num_threads(
// OGCG: call void @__kmpc_push_proc_bind(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG: call void @__kmpc_for_static_init_4(
