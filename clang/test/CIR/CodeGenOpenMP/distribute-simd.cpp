// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void distribute_simd_clause_bundle(float *p, float *q, int n) {
#pragma omp teams
  {
#pragma omp distribute simd dist_schedule(static) if(simd : n) simdlen(4) \
    safelen(8) aligned(p : 16) nontemporal(p, q)
    for (int i = 0; i < 8; ++i)
      p[i] += q[i];
  }
}

void distribute_simd_chunked(float *p, float *q, int chunk) {
#pragma omp teams
  {
#pragma omp distribute simd dist_schedule(static, chunk)
    for (unsigned i = 1; i < 17; i += 3)
      p[i] += q[i];
  }
}

void distribute_simd_loop_iv_linear(char a) {
  char i;
#pragma omp teams
  {
#pragma omp distribute simd linear(i) if(simd : a) nontemporal(i)
    for (i = a; i < 10; ++i)
      ;
  }
}

void distribute_simd_generated(float *p, float *q) {
#pragma omp teams
  {
#pragma omp distribute simd dist_schedule(static) if(simd : 1) nontemporal(p, q)
    for (int i = 8; i > 0; i += -1)
      p[i] += q[i];
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_simd_clause_bundlePfS_i(
// CIR: omp.teams {
// CIR: omp.distribute dist_schedule_static {
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) nontemporal(%{{.*}}, %{{.*}}) safelen(8) simdlen(4) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_simd_chunkedPfS_i(
// CIR: omp.teams {
// CIR: omp.distribute dist_schedule_static dist_schedule_chunk_size(%{{.*}} : i32) {
// CIR: omp.simd {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_simd_loop_iv_linearc(
// CIR: omp.teams {
// CIR: omp.distribute {
// CIR: omp.simd if(%{{.*}}) nontemporal(%{{.*}}) {
// CIR-NOT: linear(

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}distribute_simd_generatedPfS_(
// CIR: omp.teams {
// CIR: omp.distribute dist_schedule_static {
// CIR: omp.simd if(%{{.*}}) nontemporal(%{{.*}}, %{{.*}}) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// LLVM-LABEL: define internal void @_Z{{[0-9]+}}distribute_simd_clause_bundlePfS_i..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 92, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}

// LLVM-LABEL: define internal void @_Z{{[0-9]+}}distribute_simd_loop_iv_linearc..omp_par(
// LLVM-NOT: .linear_var
// LLVM-NOT: .linear_result
// LLVM: %{{.*}} = load i8, ptr %0, align 1
// LLVM: %{{.*}} = icmp ne i8 %{{.*}}, 0
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 92, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: store i8 %{{.*}}, ptr %1, align 1
// LLVM: store i8 %{{.*}}, ptr %1, align 1, !llvm.access.group !{{.*}}

// LLVM-LABEL: define internal void @_Z{{[0-9]+}}distribute_simd_generatedPfS_..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 92, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}, !llvm.access.group !{{.*}}
// LLVM: load ptr, ptr %{{.*}}, align 8, !nontemporal !{{.*}}, !llvm.access.group !{{.*}}
// LLVM: store float %{{.*}}, ptr %{{.*}}, align 4, !llvm.access.group !{{.*}}

// OGCG-LABEL: define dso_local void @_Z29distribute_simd_clause_bundlePfS_i(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z29distribute_simd_clause_bundlePfS_i.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z23distribute_simd_chunkedPfS_i(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z23distribute_simd_chunkedPfS_i.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4u(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z30distribute_simd_loop_iv_linearc(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z30distribute_simd_loop_iv_linearc.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @_Z25distribute_simd_generatedPfS_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
// OGCG-LABEL: define internal void @_Z25distribute_simd_generatedPfS_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
