// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void test_for_simd_lastprivate_conditional(int *out) {
  int a = 0;
#pragma omp parallel
  {
#pragma omp for simd lastprivate(conditional: a)
    for (int i = 0; i < 10; ++i) {
      if (i < 5)
        a = i;
    }
  }
  *out = a;
}

void test_parallel_for_simd_lastprivate_conditional(int *out) {
  int a = 0;
#pragma omp parallel for simd lastprivate(conditional: a)
  for (int i = 0; i < 10; ++i) {
    if (i < 5)
      a = i;
  }
  *out = a;
}

// CIR:       omp.critical.declare @__omp_critical___omp_lp_cond_{{.+}}
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}}.iv = #cir.int<0> : !s32i
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}} = #cir.int<0> : !s32i
// CIR-LABEL: cir.func{{.*}} @test_for_simd_lastprivate_conditional(
// CIR:       omp.parallel
// CIR:       omp.wsloop private(@{{.+}} %{{.+}} -> %[[FORSIMD_PRIV:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CIR:       omp.simd {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       cir.if %{{.+}} {
// CIR:       %[[FORSIMD_GLOBAL:.+]] = cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// CIR:       %[[FORSIMD_VAL:.+]] = cir.load align(4) %[[FORSIMD_GLOBAL]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[FORSIMD_VAL]], %[[FORSIMD_PRIV]] : !s32i, !cir.ptr<!s32i>
// CIR:       %[[FORSIMD_PRIV_VAL:.+]] = cir.load align(4) %[[FORSIMD_PRIV]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[FORSIMD_PRIV_VAL]], %{{.+}} : !s32i, !cir.ptr<!s32i>
// CIR-LABEL: cir.func{{.*}} @test_parallel_for_simd_lastprivate_conditional(
// CIR:       omp.parallel
// CIR:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %[[PFS_PRIV:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CIR:       omp.simd {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       cir.if %{{.+}} {
// CIR:       %[[PFS_GLOBAL:.+]] = cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// CIR:       %[[PFS_VAL:.+]] = cir.load align(4) %[[PFS_GLOBAL]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[PFS_VAL]], %[[PFS_PRIV]] : !s32i, !cir.ptr<!s32i>
// CIR:       %[[PFS_PRIV_VAL:.+]] = cir.load align(4) %[[PFS_PRIV]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[PFS_PRIV_VAL]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// LLVM:      @__omp_lp_cond_{{.+}}.iv = internal global i32 0
// LLVM:      @__omp_lp_cond_{{.+}} = internal global i32 0
// LLVM-LABEL: define dso_local void @test_for_simd_lastprivate_conditional(
// LLVM:      call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @test_for_simd_lastprivate_conditional..omp_par, ptr %{{.+}})
// LLVM:      define internal void @test_for_simd_lastprivate_conditional..omp_par(
// LLVM:      %{{.+}} = load i32, ptr @__omp_lp_cond_{{[^.]+}}, align 4
// LLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM-NEXT: %{{.+}} = load i32, ptr %{{.+}}, align 4
// LLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM-LABEL: define dso_local void @test_parallel_for_simd_lastprivate_conditional(
// LLVM:      call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[PFS_HELPER:test_parallel_for_simd_lastprivate_conditional\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:      define internal void @[[PFS_HELPER]](
// LLVM:      %{{.+}} = load i32, ptr @__omp_lp_cond_{{[^.]+}}, align 4
// LLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM-NEXT: %{{.+}} = load i32, ptr %{{.+}}, align 4
// LLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM:      call void @__kmpc_critical_with_hint

// OGCG-LABEL: define dso_local void @test_for_simd_lastprivate_conditional(
// OGCG: call void {{.*}}@__kmpc_fork_call
// OGCG: define internal void @test_for_simd_lastprivate_conditional.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4
// OGCG: call void @__kmpc_critical
// OGCG: call void @__kmpc_end_critical
// OGCG: call void @__kmpc_for_static_fini
// OGCG: call void @__kmpc_barrier
// OGCG-LABEL: define dso_local void @test_parallel_for_simd_lastprivate_conditional(
// OGCG: call void {{.*}}@__kmpc_fork_call
// OGCG: define internal void @test_parallel_for_simd_lastprivate_conditional.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4
// OGCG: call void @__kmpc_critical
// OGCG: call void @__kmpc_end_critical
// OGCG: call void @__kmpc_for_static_fini
