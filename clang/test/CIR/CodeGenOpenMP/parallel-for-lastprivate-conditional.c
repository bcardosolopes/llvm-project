// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void test_parallel_for_lastprivate_conditional(int *out) {
  int a = 0;
#pragma omp parallel for lastprivate(conditional: a)
  for (int i = 0; i < 10; ++i) {
    if (i < 5) {
      a = 0;
#pragma omp parallel reduction(+:a) num_threads(10)
      { a += i; }
#pragma omp atomic
      a += i;
#pragma omp parallel num_threads(10)
      {
#pragma omp atomic
        a += i;
      }
    }
  }
  *out = a;
}

void test_parallel_for_lastprivate_conditional_nested_parallel_for(int *out) {
  int a = 0;
#pragma omp parallel for lastprivate(conditional: a)
  for (int i = 0; i < 10; ++i) {
    if (i < 5) {
#pragma omp parallel for
      for (int j = 0; j < 2; ++j)
        a = i + j;
    }
  }
  *out = a;
}

void test_parallel_for_lastprivate_conditional_nested_parallel_for_simd(
    int *out) {
  int a = 0;
#pragma omp parallel for lastprivate(conditional: a)
  for (int i = 0; i < 10; ++i) {
    if (i < 5) {
#pragma omp parallel for simd
      for (int j = 0; j < 2; ++j)
        a = i + j;
    }
  }
  *out = a;
}

void test_parallel_for_lastprivate_conditional_nested_parallel_sections(
    int *out) {
  int a = 0;
#pragma omp parallel for lastprivate(conditional: a)
  for (int i = 0; i < 10; ++i) {
    if (i < 5) {
#pragma omp parallel sections
      {
        a = i;
#pragma omp section
        a = i + 1;
      }
    }
  }
  *out = a;
}

// CIR:       omp.critical.declare @__omp_critical___omp_lp_cond_{{.+}}
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}}.iv = #cir.int<0> : !s32i
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}} = #cir.int<0> : !s32i
// CIR-LABEL: cir.func{{.*}} @test_parallel_for_lastprivate_conditional(
// CIR:       omp.parallel
// CIR:       %[[FIRED:.+]] = cir.alloca !u8i, !cir.ptr<!u8i>, ["__omp_lp_cond_fired_a"]
// CIR:       omp.wsloop nowait private(@{{.+}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       omp.parallel num_threads(%{{.+}} : i32) reduction(@__omp_reduction_add_s32 %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:       cir.store volatile align(1) atomic(relaxed) %{{.+}}, %[[FIRED]] : !u8i, !cir.ptr<!u8i>
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       omp.atomic.update memory_order(relaxed) %{{.+}} : !cir.ptr<!s32i>
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       omp.parallel num_threads(%{{.+}} : i32) {
// CIR:       omp.atomic.update memory_order(relaxed) %{{.+}} : !cir.ptr<!s32i>
// CIR:       cir.store volatile align(1) atomic(relaxed) %{{.+}}, %[[FIRED]] : !u8i, !cir.ptr<!u8i>
// CIR:       %[[FIRED_VAL:.+]] = cir.load align(1) %[[FIRED]] : !cir.ptr<!u8i>, !u8i
// CIR:       %[[ZERO:.+]] = cir.const #cir.int<0> : !u8i
// CIR:       %[[SHOULD_UPDATE:.+]] = cir.cmp(ne, %[[FIRED_VAL]], %[[ZERO]]) : !u8i, !cir.bool
// CIR:       cir.if %[[SHOULD_UPDATE]] {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})

// LLVM:      @__omp_lp_cond_{{.+}}.iv = internal global i32 0
// LLVM:      @__omp_lp_cond_{{.+}} = internal global i32 0
// LLVM-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional(
// LLVM:      call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[TOP_HELPER:test_parallel_for_lastprivate_conditional\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:      define internal void @[[TOP_HELPER]](
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[RED_HELPER:test_parallel_for_lastprivate_conditional\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      %{{.+}} = atomicrmw add ptr %{{.+}}, i32 %{{.+}} monotonic, align 4
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[SHARED_HELPER:test_parallel_for_lastprivate_conditional\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:      %[[FIRED:.+]] = load i8, ptr %{{.+}}, align 1
// LLVM:      %[[SHOULD_UPDATE:.+]] = icmp ne i8 %[[FIRED]], 0
// LLVM:      br i1 %[[SHOULD_UPDATE]], label %{{.+}}, label %{{.+}}
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      define internal void @[[SHARED_HELPER]](
// LLVM:      %{{.+}} = atomicrmw add ptr %{{.+}}, i32 %{{.+}} monotonic, align 4
// LLVM:      store atomic volatile i8 1, ptr %{{.+}} monotonic, align 1
// LLVM:      define internal void @[[RED_HELPER]](
// LLVM:      store atomic volatile i8 1, ptr %{{.+}} monotonic, align 1

// CIR-LABEL: cir.func{{.*}} @test_parallel_for_lastprivate_conditional_nested_parallel_for(
// CIR:       omp.parallel
// CIR:       %[[NESTPF_FIRED:.+]] = cir.alloca !u8i, !cir.ptr<!u8i>, ["__omp_lp_cond_fired_a"]
// CIR:       omp.parallel
// CIR:       omp.wsloop nowait {
// CIR:       cir.store volatile align(1) atomic(relaxed) %{{.+}}, %[[NESTPF_FIRED]] : !u8i, !cir.ptr<!u8i>
// CIR:       %[[NESTPF_FIRED_VAL:.+]] = cir.load align(1) %[[NESTPF_FIRED]] : !cir.ptr<!u8i>, !u8i
// CIR:       %[[NESTPF_ZERO:.+]] = cir.const #cir.int<0> : !u8i
// CIR:       %[[NESTPF_SHOULD_UPDATE:.+]] = cir.cmp(ne, %[[NESTPF_FIRED_VAL]], %[[NESTPF_ZERO]]) : !u8i, !cir.bool
// CIR:       cir.if %[[NESTPF_SHOULD_UPDATE]] {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})

// CIR-LABEL: cir.func{{.*}} @test_parallel_for_lastprivate_conditional_nested_parallel_for_simd(
// CIR:       omp.parallel
// CIR:       %[[NESTPFS_FIRED:.+]] = cir.alloca !u8i, !cir.ptr<!u8i>, ["__omp_lp_cond_fired_a"]
// CIR:       omp.parallel
// CIR:       omp.wsloop nowait {
// CIR:       omp.simd {
// CIR:       cir.store volatile align(1) atomic(relaxed) %{{.+}}, %[[NESTPFS_FIRED]] : !u8i, !cir.ptr<!u8i>
// CIR:       %[[NESTPFS_FIRED_VAL:.+]] = cir.load align(1) %[[NESTPFS_FIRED]] : !cir.ptr<!u8i>, !u8i
// CIR:       %[[NESTPFS_ZERO:.+]] = cir.const #cir.int<0> : !u8i
// CIR:       %[[NESTPFS_SHOULD_UPDATE:.+]] = cir.cmp(ne, %[[NESTPFS_FIRED_VAL]], %[[NESTPFS_ZERO]]) : !u8i, !cir.bool
// CIR:       cir.if %[[NESTPFS_SHOULD_UPDATE]] {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})

// CIR-LABEL: cir.func{{.*}} @test_parallel_for_lastprivate_conditional_nested_parallel_sections(
// CIR:       omp.parallel
// CIR:       %[[NESTPS_FIRED:.+]] = cir.alloca !u8i, !cir.ptr<!u8i>, ["__omp_lp_cond_fired_a"]
// CIR:       omp.parallel {
// CIR:       omp.sections nowait {
// CIR:       omp.section {
// CIR:       cir.store volatile align(1) atomic(relaxed) %{{.+}}, %[[NESTPS_FIRED]] : !u8i, !cir.ptr<!u8i>
// CIR:       %[[NESTPS_FIRED_VAL:.+]] = cir.load align(1) %[[NESTPS_FIRED]] : !cir.ptr<!u8i>, !u8i
// CIR:       %[[NESTPS_ZERO:.+]] = cir.const #cir.int<0> : !u8i
// CIR:       %[[NESTPS_SHOULD_UPDATE:.+]] = cir.cmp(ne, %[[NESTPS_FIRED_VAL]], %[[NESTPS_ZERO]]) : !u8i, !cir.bool
// CIR:       cir.if %[[NESTPS_SHOULD_UPDATE]] {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})

// LLVM-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional_nested_parallel_for(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[NESTPF_OUTER:test_parallel_for_lastprivate_conditional_nested_parallel_for\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:       define internal void @[[NESTPF_OUTER]](
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[NESTPF_INNER:test_parallel_for_lastprivate_conditional_nested_parallel_for\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:       %[[NESTPF_FIRED_VAL:.+]] = load i8, ptr %{{.+}}, align 1
// LLVM:       %[[NESTPF_SHOULD_UPDATE:.+]] = icmp ne i8 %[[NESTPF_FIRED_VAL]], 0
// LLVM:       br i1 %[[NESTPF_SHOULD_UPDATE]], label %{{.+}}, label %{{.+}}
// LLVM:       call void @__kmpc_critical_with_hint
// LLVM:       define internal void @[[NESTPF_INNER]](
// LLVM:       call void @__kmpc_for_static_init_4(
// LLVM:       store atomic volatile i8 1, ptr %{{.+}} monotonic, align 1

// LLVM-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional_nested_parallel_for_simd(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[NESTPFS_OUTER:test_parallel_for_lastprivate_conditional_nested_parallel_for_simd\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:       define internal void @[[NESTPFS_OUTER]](
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[NESTPFS_INNER:test_parallel_for_lastprivate_conditional_nested_parallel_for_simd\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:       %[[NESTPFS_FIRED_VAL:.+]] = load i8, ptr %{{.+}}, align 1
// LLVM:       %[[NESTPFS_SHOULD_UPDATE:.+]] = icmp ne i8 %[[NESTPFS_FIRED_VAL]], 0
// LLVM:       br i1 %[[NESTPFS_SHOULD_UPDATE]], label %{{.+}}, label %{{.+}}
// LLVM:       call void @__kmpc_critical_with_hint
// LLVM:       define internal void @[[NESTPFS_INNER]](
// LLVM:       call void @__kmpc_for_static_init_4(
// LLVM:       store atomic volatile i8 1, ptr %{{.+}} monotonic, align 1

// LLVM-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional_nested_parallel_sections(
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[NESTPS_OUTER:test_parallel_for_lastprivate_conditional_nested_parallel_sections\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:       define internal void @[[NESTPS_OUTER]](
// LLVM:       call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{[^,]+}}, i32 1, ptr @[[NESTPS_INNER:test_parallel_for_lastprivate_conditional_nested_parallel_sections\.\.omp_par[^,]*]], ptr %{{.+}})
// LLVM:       %[[NESTPS_FIRED_VAL:.+]] = load i8, ptr %{{.+}}, align 1
// LLVM:       %[[NESTPS_SHOULD_UPDATE:.+]] = icmp ne i8 %[[NESTPS_FIRED_VAL]], 0
// LLVM:       br i1 %[[NESTPS_SHOULD_UPDATE]], label %{{.+}}, label %{{.+}}
// LLVM:       call void @__kmpc_critical_with_hint
// LLVM:       define internal void @[[NESTPS_INNER]](
// LLVM:       call void @__kmpc_for_static_init_4u(
// LLVM:       store atomic volatile i8 1, ptr %{{.+}} monotonic, align 1

// OGCG-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @test_parallel_for_lastprivate_conditional.omp_outlined

// OGCG-LABEL: define internal void @test_parallel_for_lastprivate_conditional.omp_outlined
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_push_num_threads(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 2, ptr @test_parallel_for_lastprivate_conditional.omp_outlined.omp_outlined
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: atomicrmw add ptr %{{.+}}, i32 %{{.+}} monotonic
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_push_num_threads(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional_nested_parallel_for(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @test_parallel_for_lastprivate_conditional_nested_parallel_for.omp_outlined

// OGCG-LABEL: define internal void @test_parallel_for_lastprivate_conditional_nested_parallel_for.omp_outlined
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional_nested_parallel_for_simd(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @test_parallel_for_lastprivate_conditional_nested_parallel_for_simd.omp_outlined

// OGCG-LABEL: define internal void @test_parallel_for_lastprivate_conditional_nested_parallel_for_simd.omp_outlined
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @test_parallel_for_lastprivate_conditional_nested_parallel_sections(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @test_parallel_for_lastprivate_conditional_nested_parallel_sections.omp_outlined

// OGCG-LABEL: define internal void @test_parallel_for_lastprivate_conditional_nested_parallel_sections.omp_outlined
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_for_static_fini(
