// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

typedef __UINTPTR_TYPE__ omp_event_handle_t;

void body(void);

void task_detach_simple(omp_event_handle_t *out) {
  omp_event_handle_t evt = 0;
#pragma omp task detach(evt)
  body();
  *out = evt;
}

// CIR-LABEL: cir.func{{.*}} @task_detach_simple(
// CIR: %[[EVT:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["evt", init]
// CIR: omp.task detach(%[[EVT]] : !cir.ptr<!u64i>) private(@{{.*}} %[[EVT]] -> %arg{{.*}} : !cir.ptr<!u64i>) {
// CIR:   cir.call @body() : () -> ()
// CIR:   omp.terminator
// CIR: }

// LLVM-LABEL: define dso_local void @task_detach_simple(
// LLVM: %[[EVT:.*]] = alloca i64, i64 1, align 8
// LLVM: %[[GTID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: %[[ALLOC:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @{{.*}}, i32 %[[GTID]], i32 65, i64 {{[0-9]+}}, i64 {{[0-9]+}}, ptr @task_detach_simple..omp_par)
// LLVM: %[[EVTVAL:.*]] = call ptr @__kmpc_task_allow_completion_event(ptr @{{.*}}, i32 %[[GTID]], ptr %[[ALLOC]])
// LLVM: %[[CAST:.*]] = ptrtoint ptr %[[EVTVAL]] to i64
// LLVM: store i64 %[[CAST]], ptr %[[EVT]], align 8

// OGCG-LABEL: define {{.*}} @task_detach_simple(
// OGCG:         call ptr @__kmpc_omp_task_alloc({{.*}}, i32 65,
// OGCG:         call ptr @__kmpc_task_allow_completion_event(
// OGCG:         call i32 @__kmpc_omp_task(
