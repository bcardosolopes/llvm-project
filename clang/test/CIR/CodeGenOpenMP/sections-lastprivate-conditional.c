// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sections_lastprivate(int *out) {
  int a = 0;
#pragma omp parallel
  {
#pragma omp sections lastprivate(a)
    {
      a = 1;
#pragma omp section
      a = 2;
    }
  }
  *out = a;
}

void sections_lastprivate_conditional(int *out) {
  int a = 0;
#pragma omp parallel
  {
#pragma omp sections lastprivate(conditional: a)
    {
      if (*out)
        a = 1;
#pragma omp section
      if (!*out)
        a = 2;
    }
  }
  *out = a;
}

void parallel_sections_lastprivate_conditional(int *out) {
  int a = 0;
#pragma omp parallel sections lastprivate(conditional: a)
  {
    if (*out)
      a = 1;
#pragma omp section
    if (!*out)
      a = 2;
  }
  *out = a;
}

void parallel_sections_first_and_lastprivate(int *out, int x) {
#pragma omp parallel sections firstprivate(x) lastprivate(x)
  {
    x = 1;
#pragma omp section
    x = 2;
  }
  *out = x;
}

// CIR:       omp.critical.declare @__omp_critical___omp_lp_cond_{{.+}}
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}}.iv = #cir.int<0> : !s32i
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}} = #cir.int<0> : !s32i

// CIR-LABEL: cir.func{{.*}} @sections_lastprivate(
// CIR:       omp.parallel
// CIR:       omp.wsloop private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:       cir.switch(%{{.+}} : !s32i) {
// CIR:       cir.case(equal, [#cir.int<0> : !s32i])
// CIR:       cir.case(equal, [#cir.int<1> : !s32i])
// CIR:       cir.if %{{.+}} {
// CIR:       %{{.+}} = cir.load align(4) %{{.+}} : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %{{.+}}, %{{.+}} : !s32i, !cir.ptr<!s32i>

// CIR-LABEL: cir.func{{.*}} @sections_lastprivate_conditional(
// CIR:       omp.parallel
// CIR:       omp.wsloop private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:       cir.case(equal, [#cir.int<0> : !s32i])
// CIR:       cir.case(equal, [#cir.int<1> : !s32i])
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       %[[COND_GLOBAL:.+]] = cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// CIR:       %[[COND_VAL:.+]] = cir.load align(4) %[[COND_GLOBAL]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store align(4) %[[COND_VAL]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// CIR-LABEL: cir.func{{.*}} @parallel_sections_lastprivate_conditional(
// CIR:       omp.parallel
// CIR:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:       cir.case(equal, [#cir.int<0> : !s32i])
// CIR:       cir.case(equal, [#cir.int<1> : !s32i])

// CIR-LABEL: cir.func{{.*}} @parallel_sections_first_and_lastprivate(
// CIR:       omp.parallel private(@{{.+}} %{{.+}} -> %arg2 : !cir.ptr<!s32i>) {
// CIR:       omp.wsloop nowait private(@{{.+}} %arg2 -> %arg3 : !cir.ptr<!s32i>) private_barrier {

// LLVM:      @__omp_lp_cond_{{.+}}.iv = internal global i32 0
// LLVM:      @__omp_lp_cond_{{.+}} = internal global i32 0

// LLVM-LABEL: define dso_local void @sections_lastprivate(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call
// LLVM-LABEL: define internal void @sections_lastprivate..omp_par(
// LLVM:       call void @__kmpc_for_static_init_4
// LLVM:       call void @__kmpc_for_static_fini
// LLVM:       call void @__kmpc_barrier
// LLVM:       switch i32 %{{.+}}, label %{{.+}} [
// LLVM-NEXT:    i32 0, label %{{.+}}
// LLVM-NEXT:    i32 1, label %{{.+}}
// LLVM:       %[[NEXT:.+]] = add i32 %{{.+}}, 1
// LLVM:       %[[ISLAST:.+]] = icmp sgt i32 %[[NEXT]], 1
// LLVM:       br i1 %[[ISLAST]], label %[[COPY:.+]], label %{{.+}}
// LLVM:       [[COPY]]:
// LLVM:       %[[COPYVAL:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM:       store i32 %[[COPYVAL]], ptr %{{.+}}, align 4

// LLVM-LABEL: define dso_local void @sections_lastprivate_conditional(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call
// LLVM-LABEL: define internal void @sections_lastprivate_conditional..omp_par(
// LLVM:       call void @__kmpc_for_static_init_4
// LLVM:       call void @__kmpc_for_static_fini
// LLVM:       call void @__kmpc_barrier
// LLVM:       call void @__kmpc_critical_with_hint
// LLVM:       %{{.+}} = load i32, ptr @__omp_lp_cond_{{.+}}, align 4
// LLVM:       store i32 %{{.+}}, ptr %{{.+}}, align 4
// LLVM:       store i32 %{{.+}}, ptr %{{.+}}, align 4

// LLVM-LABEL: define dso_local void @parallel_sections_lastprivate_conditional(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call
// LLVM-LABEL: define internal void @parallel_sections_lastprivate_conditional..omp_par(
// LLVM:       call void @__kmpc_for_static_init_4
// LLVM:       call void @__kmpc_for_static_fini
// LLVM-NOT:   call void @__kmpc_barrier
// LLVM:       call void @__kmpc_critical_with_hint
// LLVM:       %{{.+}} = load i32, ptr @__omp_lp_cond_{{.+}}, align 4
// LLVM:       store i32 %{{.+}}, ptr %{{.+}}, align 4

// LLVM-LABEL: define dso_local void @parallel_sections_first_and_lastprivate(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call
// LLVM-LABEL: define internal void @parallel_sections_first_and_lastprivate..omp_par(
// LLVM:       call void @__kmpc_barrier
// LLVM:       call void @__kmpc_for_static_init_4

// OGCG-LABEL: define {{.*}} @sections_lastprivate(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 1, ptr @sections_lastprivate.omp_outlined,
// OGCG-LABEL: define internal void @sections_lastprivate.omp_outlined(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @sections_lastprivate_conditional(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 2, ptr @sections_lastprivate_conditional.omp_outlined,
// OGCG-LABEL: define internal void @sections_lastprivate_conditional.omp_outlined(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_critical(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @parallel_sections_lastprivate_conditional(
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG-LABEL: define internal void @parallel_sections_lastprivate_conditional.omp_outlined(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_critical(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @parallel_sections_first_and_lastprivate(
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG-LABEL: define internal void @parallel_sections_first_and_lastprivate.omp_outlined(
// OGCG:         call void @__kmpc_barrier(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_for_static_fini(
