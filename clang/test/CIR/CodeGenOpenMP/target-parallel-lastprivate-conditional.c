// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=TARGET
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOSTLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void target_parallel_for_lastprivate_conditional(void) {
  int last = 0;
#pragma omp target parallel for lastprivate(conditional: last)
  for (int i = 0; i < 10; ++i) {
    if (i < 5)
      last = i;
  }
}

void target_parallel_for_simd_lastprivate_conditional(void) {
  int last = 0;
#pragma omp target parallel for simd lastprivate(conditional: last)
  for (int i = 0; i < 10; ++i) {
    if (i < 5)
      last = i;
  }
}

// HOST:       omp.critical.declare @__omp_critical___omp_lp_cond_{{.+}}
// HOST:       cir.global "private"  internal  @__omp_lp_cond_{{.+}}.iv = #cir.int<0> : !s32i
// HOST:       cir.global "private"  internal  @__omp_lp_cond_{{.+}} = #cir.int<0> : !s32i
// HOST-LABEL: cir.func{{.*}} @target_parallel_for_lastprivate_conditional(
// HOST:       omp.parallel
// HOST:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %[[TPF_PRIV:arg[0-9]+]] : !cir.ptr<!s32i>) {
// HOST:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// HOST:       cir.if %{{.+}} {
// HOST:       %[[TPF_GLOBAL:.+]] = cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// HOST:       %[[TPF_VAL:.+]] = cir.load align(4) %[[TPF_GLOBAL]] : !cir.ptr<!s32i>, !s32i
// HOST:       cir.store align(4) %[[TPF_VAL]], %[[TPF_PRIV]] : !s32i, !cir.ptr<!s32i>
// HOST:       %[[TPF_PRIV_VAL:.+]] = cir.load align(4) %[[TPF_PRIV]] : !cir.ptr<!s32i>, !s32i
// HOST:       cir.store align(4) %[[TPF_PRIV_VAL]], %{{.+}} : !s32i, !cir.ptr<!s32i>
// HOST-LABEL: cir.func{{.*}} @target_parallel_for_simd_lastprivate_conditional(
// HOST:       omp.parallel
// HOST:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %[[TPFS_PRIV:arg[0-9]+]] : !cir.ptr<!s32i>) {
// HOST:       omp.simd {
// HOST:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// HOST:       cir.if %{{.+}} {
// HOST:       %[[TPFS_GLOBAL:.+]] = cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// HOST:       %[[TPFS_VAL:.+]] = cir.load align(4) %[[TPFS_GLOBAL]] : !cir.ptr<!s32i>, !s32i
// HOST:       cir.store align(4) %[[TPFS_VAL]], %[[TPFS_PRIV]] : !s32i, !cir.ptr<!s32i>
// HOST:       %[[TPFS_PRIV_VAL:.+]] = cir.load align(4) %[[TPFS_PRIV]] : !cir.ptr<!s32i>, !s32i
// HOST:       cir.store align(4) %[[TPFS_PRIV_VAL]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// TARGET-LABEL: cir.func{{.*}} @target_parallel_for_lastprivate_conditional(
// TARGET:       omp.target
// TARGET:       omp.parallel
// TARGET:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>)
// TARGET:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// TARGET-LABEL: cir.func{{.*}} @target_parallel_for_simd_lastprivate_conditional(
// TARGET:       omp.target
// TARGET:       omp.parallel
// TARGET:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>)
// TARGET:       omp.simd {
// TARGET:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})

// HOSTLLVM:      @__omp_lp_cond_{{.+}}.iv = internal global i32 0
// HOSTLLVM:      @__omp_lp_cond_{{.+}} = internal global i32 0
// HOSTLLVM-LABEL: define dso_local void @target_parallel_for_lastprivate_conditional(
// HOSTLLVM:      define internal void @[[TPF_HELPER:target_parallel_for_lastprivate_conditional\.\.omp_par[^,]*]](
// HOSTLLVM:      %{{.+}} = load i32, ptr @__omp_lp_cond_{{[^.]+}}, align 4
// HOSTLLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// HOSTLLVM:      call void @__kmpc_critical_with_hint
// HOSTLLVM-LABEL: define dso_local void @target_parallel_for_simd_lastprivate_conditional(
// HOSTLLVM:      define internal void @[[TPFS_HELPER:target_parallel_for_simd_lastprivate_conditional\.\.omp_par[^,]*]](
// HOSTLLVM:      %{{.+}} = load i32, ptr @__omp_lp_cond_{{[^.]+}}, align 4
// HOSTLLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// HOSTLLVM:      call void @__kmpc_critical_with_hint

// OGCG-LABEL: define dso_local void @target_parallel_for_lastprivate_conditional(
// OGCG:         call void @__omp_offloading_{{.*}}target_parallel_for_lastprivate_conditional{{.*}}(
// OGCG:         define internal void @__omp_offloading_{{.*}}target_parallel_for_lastprivate_conditional
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_critical(

// OGCG-LABEL: define dso_local void @target_parallel_for_simd_lastprivate_conditional(
// OGCG:         call void @__omp_offloading_{{.*}}target_parallel_for_simd_lastprivate_conditional{{.*}}(
// OGCG:         define internal void @__omp_offloading_{{.*}}target_parallel_for_simd_lastprivate_conditional
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_critical(
