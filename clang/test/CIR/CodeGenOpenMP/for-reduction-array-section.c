// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// The loop body names an array-section reduction through its base variable,
// so the base must resolve into the private copy, shifted back by the
// section's offset.

int v[4];

// CIR-LABEL: cir.func{{.*}} @const_section(
// CIR: %[[START:.*]] = cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>
// CIR: %[[V:.*]] = cir.get_global @v : !cir.ptr<!cir.array<!s32i x 4>>
// CIR: %[[V_ELEM:.*]] = cir.cast bitcast %[[V]] : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
// CIR: %[[OFF:.*]] = cir.ptr_diff %[[START]], %[[V_ELEM]] : !cir.ptr<!s32i> -> !s64i
// CIR: omp.wsloop nowait reduction(byref @{{.*}} %[[START]] -> %[[PRIV:.*]] : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest
// CIR: %[[NEG:.*]] = cir.unary(minus, %[[OFF]]) : !s64i, !s64i
// CIR: %[[BASE:.*]] = cir.ptr_stride %[[PRIV]], %[[NEG]] : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>
// CIR: %[[ARR:.*]] = cir.cast bitcast %[[BASE]] : !cir.ptr<!s32i> -> !cir.ptr<!cir.array<!s32i x 4>>
// CIR: cir.do {
// CIR: cir.get_element %[[ARR]][%{{.*}} : !s32i] : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
void const_section(int n) {
#pragma omp parallel for reduction(+: v[1:2])
  for (int i = 0; i < n; ++i)
    v[1] += i;
}

// A pointer base gets a per-thread slot outside the loop. The runtime element
// count is stored before the private copy is allocated with it.
// CIR-LABEL: cir.func{{.*}} @pointer_section(
// CIR: %[[COUNT_DIFF:.*]] = cir.ptr_diff %{{.*}}, %{{.*}} : !cir.ptr<!s32i> -> !s64i
// CIR-NEXT: cir.cast integral %[[COUNT_DIFF]]
// CIR: %[[OFF:.*]] = cir.ptr_diff %{{.*}}, %{{.*}} : !cir.ptr<!s32i> -> !s64i
// CIR: %[[SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["omp.reduction.section.base"]
// CIR: %[[NEG:.*]] = cir.unary(minus, %[[OFF]]) : !s64i, !s64i
// CIR: omp.wsloop reduction(byref @{{.*}} %{{.*}} -> %[[PRIV:.*]] : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest
// CIR: %[[BASE:.*]] = cir.ptr_stride %[[PRIV]], %[[NEG]] : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>
// CIR: cir.store align(8) %[[BASE]], %[[SLOT]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR: cir.do {
// CIR: %[[P:.*]] = cir.load align(8) %[[SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR: cir.load align(4) %[[P]] : !cir.ptr<!s32i>, !s32i

// LLVM-LABEL: define internal void @pointer_section..omp_par(
// LLVM: %[[COUNT:.*]] = add i64 %{{.*}}, 1
// LLVM: %[[SET_TLS:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %{{.*}}, ptr @[[SIZE:__omp_task_reduction_size_p_[0-9]+_[0-9]+]], i64 8, ptr @[[SIZE]].cache)
// LLVM: store i64 %[[COUNT]], ptr %[[SET_TLS]], align 8
// LLVM: %[[N:.*]] = load i64, ptr %{{.*}}, align 8
// LLVM-NEXT: %{{.*}} = alloca i32, i64 %[[N]], align 16
void pointer_section(int *p, int m, int n) {
#pragma omp parallel
#pragma omp for reduction(+: p[:m])
  for (int i = 0; i < n; ++i)
    p[0] += i;
}
