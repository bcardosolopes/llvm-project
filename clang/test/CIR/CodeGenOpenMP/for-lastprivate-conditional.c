// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void test_for_lastprivate(int *out) {
  int a = 0;
#pragma omp parallel
  {
#pragma omp for lastprivate(a)
    for (int i = 0; i < 10; ++i)
      a = i;
  }
  *out = a;
}

void test_for_lastprivate_conditional(int *out) {
  int a = 0;
#pragma omp parallel
  {
#pragma omp for lastprivate(conditional: a)
    for (int i = 0; i < 10; ++i) {
      if (i < 5)
        a = i;
    }
  }
  *out = a;
}

// CIR:       omp.critical.declare @__omp_critical___omp_lp_cond_{{.+}}
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}}.iv = #cir.int<0> : !s32i
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}} = #cir.int<0> : !s32i
// CIR-LABEL: cir.func{{.*}} @test_for_lastprivate(
// CIR:       omp.parallel
// CIR:       omp.wsloop private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:       cir.if %{{.+}} {
// CIR-LABEL: cir.func{{.*}} @test_for_lastprivate_conditional(
// CIR:       omp.parallel
// CIR:       omp.wsloop private(@{{.+}} %{{.+}} -> %[[COND_PRIVATE:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       cir.if %{{.+}} {
// CIR:       %[[COND_GLOBAL:.+]] = cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// CIR:       %[[COND_VAL:.+]] = cir.load align(4) %[[COND_GLOBAL]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[COND_VAL]], %[[COND_PRIVATE]] : !s32i, !cir.ptr<!s32i>
// CIR:       %[[COND_PRIVATE_VAL:.+]] = cir.load align(4) %[[COND_PRIVATE]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[COND_PRIVATE_VAL]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// LLVM:      @__omp_lp_cond_{{.+}}.iv = internal global i32 0
// LLVM:      @__omp_lp_cond_{{.+}} = internal global i32 0
// LLVM-LABEL: define dso_local void @test_for_lastprivate_conditional(
// LLVM:      call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @test_for_lastprivate_conditional..omp_par, ptr %{{.+}})
// LLVM:      define internal void @test_for_lastprivate_conditional..omp_par(
// LLVM:      call void @__kmpc_for_static_init_4
// LLVM:      call void @__kmpc_for_static_fini
// LLVM:      call void @__kmpc_barrier
// LLVM:      %{{.+}} = load i32, ptr @__omp_lp_cond_{{.+}}, align 4
// LLVM:      store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM:      store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      store i32 %{{.+}}, ptr @__omp_lp_cond_{{.+}}.iv, align 4
// LLVM:      store i32 %{{.+}}, ptr @__omp_lp_cond_{{.+}}, align 4

// OGCG-LABEL: define dso_local void @test_for_lastprivate(
// OGCG-LABEL: define internal void @test_for_lastprivate.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
// OGCG: call void @__kmpc_barrier(

// OGCG-LABEL: define dso_local void @test_for_lastprivate_conditional(
// OGCG-LABEL: define internal void @test_for_lastprivate_conditional.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_for_static_fini(
// OGCG: call void @__kmpc_barrier(
// OGCG: call void @__kmpc_barrier(
