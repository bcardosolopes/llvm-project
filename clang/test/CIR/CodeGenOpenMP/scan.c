// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

void use_int(int);
int g;
int v[3];

// The scan phase enters the loop body at the scan directive, so the loop
// variable is materialized again after the omp.scan.
// CIR-LABEL: cir.func{{.*}} @simd_scan(
// CIR: omp.simd reduction(mod: inscan, @__omp_reduction_add_s32 %{{.*}} -> %[[ACC:.*]] : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest (%[[IV:.*]]) : i32
// CIR: cir.store align(4) %{{.*}}, %[[OMP_IV:.*]] : !s32i, !cir.ptr<!s32i>
// CIR: cir.store align(4) %{{.*}}, %[[I:.*]] : !s32i, !cir.ptr<!s32i>
// CIR: cir.do {
// CIR: omp.scan inclusive(%[[ACC]] : !cir.ptr<!s32i>)
// CIR-NEXT: %[[IV2:.*]] = builtin.unrealized_conversion_cast %[[IV]] : i32 to !s32i
// CIR-NEXT: cir.store align(4) %[[IV2]], %[[OMP_IV]] : !s32i, !cir.ptr<!s32i>
// CIR: cir.store align(4) %{{.*}}, %[[I]] : !s32i, !cir.ptr<!s32i>
// CIR-NEXT: %[[V:.*]] = cir.load align(4) %[[ACC]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT: cir.call @use_int(%[[V]])
void simd_scan(int *a, int n) {
  int acc = 0;
#pragma omp simd reduction(inscan, +: acc)
  for (int i = 0; i < n; ++i) {
    acc += a[i];
#pragma omp scan inclusive(acc)
    use_int(acc);
  }
}

// One thread allocates the scan buffer and broadcasts its address; the buffer
// is freed only after the barrier that ends the scan-phase loop.
// CIR-LABEL: cir.func{{.*}} @parallel_for_scan(
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %[[I:.*]] : !cir.ptr<!s32i>) {
// CIR: omp.wsloop nowait reduction(mod: inscan, @__omp_reduction_add_s32 %{{.*}} -> %[[ACC:.*]] : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest (%[[IV:.*]]) : i32
// CIR: omp.scan exclusive(%[[ACC]] : !cir.ptr<!s32i>)
// CIR-NEXT: %[[IV2:.*]] = builtin.unrealized_conversion_cast %[[IV]] : i32 to !s32i
// CIR-NEXT: cir.store align(4) %[[IV2]], %[[I]] : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: define internal void @parallel_for_scan..omp_par(
// LLVM: %[[BUF:.*]] = alloca ptr, align 8
// LLVM: %scan.didalloc = alloca i32, align 4
// LLVM: store i32 0, ptr %scan.didalloc, align 4
// LLVM: call i32 @__kmpc_masked(
// LLVM: %[[DIDIT:.*]] = load i32, ptr %scan.didalloc, align 4
// LLVM: call void @__kmpc_copyprivate(ptr @{{.*}}, i32 %{{.*}}, i64 0, ptr %[[BUF]], ptr @.omp.scan.buffer.ptr.copy, i32 %[[DIDIT]])
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_barrier(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_barrier(
// LLVM: call i32 @__kmpc_masked(
// LLVM: call void @free(
// LLVM: %[[ARR:.*]] = tail call ptr @malloc(
// LLVM-NEXT: store ptr %[[ARR]], ptr %[[BUF]], align 8
// LLVM-NEXT: store i32 1, ptr %scan.didalloc, align 4
void parallel_for_scan(int *a, int n) {
  int acc = 0;
#pragma omp parallel for reduction(inscan, +: acc)
  for (int i = 0; i < n; ++i) {
    use_int(acc);
#pragma omp scan exclusive(acc)
    acc += a[i];
  }
}

// An array-section scan buffers whole sections: each slot is copied with
// memcpy, and the body reaches the private copy through `v`.
// CIR-LABEL: cir.func{{.*}} @section_scan(
// CIR: %[[OFF:.*]] = cir.ptr_diff %{{.*}}, %{{.*}} : !cir.ptr<!s32i> -> !s64i
// CIR: omp.wsloop nowait reduction(mod: inscan, byref @{{.*}} %{{.*}} -> %[[PRIV:.*]] : !cir.ptr<!s32i>) {
// CIR: %[[NEG:.*]] = cir.unary(minus, %[[OFF]]) : !s64i, !s64i
// CIR-NEXT: %[[BASE:.*]] = cir.ptr_stride %[[PRIV]], %[[NEG]] : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>
// CIR-NEXT: %[[ARR:.*]] = cir.cast bitcast %[[BASE]] : !cir.ptr<!s32i> -> !cir.ptr<!cir.array<!s32i x 3>>
// CIR-NEXT: cir.get_element %[[ARR]][%{{.*}} : !s32i]
// CIR: omp.scan inclusive(%[[PRIV]] : !cir.ptr<!s32i>)

// LLVM-LABEL: define internal void @section_scan..omp_par(
// LLVM: %[[LAST:omp.scan.slot[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr getelementptr inbounds nuw (i8, ptr @v, i64 4), ptr %[[LAST]], i64 8, i1 false)
// LLVM: %[[LOAD:omp.scan.slot[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr %[[PRIV:.*]], ptr %[[LOAD]], i64 8, i1 false)
// LLVM: %omp.scan.slot = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr %omp.scan.slot, ptr %[[PRIV]], i64 8, i1 false)
void section_scan(int *a, int n) {
#pragma omp parallel for reduction(inscan, +: v[1:2])
  for (int i = 0; i < n; ++i) {
    v[1] += a[i];
#pragma omp scan inclusive(v[1:2])
    use_int(v[2]);
  }
}

void for_simd_global_scan(int *a, int n) {
#pragma omp parallel
  {
#pragma omp for simd reduction(inscan, +: g)
    for (int i = 0; i < n; ++i) {
      g += a[i];
#pragma omp scan inclusive(g)
      use_int(g);
    }
  }
}

void parallel_for_simd_dynamic_section_scan(int *a, int *s, int n, int m) {
#pragma omp parallel for simd reduction(inscan, +: s[:m])
  for (int i = 0; i < n; ++i) {
    use_int(s[0]);
#pragma omp scan exclusive(s[:m])
    s[0] += a[i];
  }
}

// LLVM-LABEL: define internal void @.omp.scan.buffer.ptr.copy(ptr %0, ptr %1)
// LLVM: %[[P:.*]] = load ptr, ptr %1, align 8
// LLVM: store ptr %[[P]], ptr %0, align 8
