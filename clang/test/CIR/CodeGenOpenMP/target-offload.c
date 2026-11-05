// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int device(void);
void work(int *, int);

void target_scalar_if_device(int *a, int cond) {
// CIR-LABEL: cir.func{{.*}} @target_scalar_if_device(
// CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(tofrom) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "a"}
// CIR: %[[CONDMAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(implicit) capture(ByCopy) -> !cir.ptr<!s32i>
// CIR: omp.target device(%{{.*}} : i32) if(%{{.*}}) map_entries(%[[CONDMAP]] -> %{{.*}}, %[[MAP]] -> %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>) private(@__omp_firstprivate_cond_{{[0-9_\.]+}} %{{.*}} -> %{{.*}} [map_idx=0] : !cir.ptr<!s32i>) {
#pragma omp target map(tofrom: a) if(cond) device(device())
  work(a, cond);
}

void target_array_section(int *a) {
  // CIR-LABEL: cir.func{{.*}} @target_array_section(
  // CIR: %[[BOUND:.*]] = omp.map.bounds
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%[[BOUND]]) -> !cir.ptr<!s32i> {name = "a[0:4]"}
  // CIR: omp.target map_entries(%[[MAP]] -> %{{.*}} : !cir.ptr<!s32i>) {
  // CIR-NEXT: %[[TMP:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["a"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %{{.*}}, %[[TMP]]
  // CIR: cir.call @work
#pragma omp target map(tofrom: a[0:4])
  work(a, 1);
}

// LLVM: @.offload_sizes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 16, i64 8]
// LLVM: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 35, i64 16384]
// LLVM-LABEL: define dso_local void @target_scalar_if_device(
// LLVM: call i32 @device()
// LLVM: call i32 @__tgt_target_kernel(
// LLVM-LABEL: define dso_local void @target_array_section(
// LLVM: %.offload_baseptrs = alloca [2 x ptr], align 8
// LLVM: %.offload_ptrs = alloca [2 x ptr], align 8
// LLVM: call i32 @__tgt_target_kernel(

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_scalar_if_device{{.*}}(i64 noundef %0, ptr noundef nonnull align 8 dereferenceable(8) %1)
// LLVM: %[[COND_SLOT:.*]] = alloca i64, align 8
// LLVM: store i64 %0, ptr %[[COND_SLOT]], align 8
// LLVM: %[[COND_PRIV:.*]] = alloca i32, align 4
// LLVM: %[[COND_VAL:.*]] = load i32, ptr %[[COND_SLOT]], align 4
// LLVM: store i32 %[[COND_VAL]], ptr %[[COND_PRIV]], align 4
// LLVM: %[[A:.*]] = load ptr, ptr %1, align 8
// LLVM: %[[COND_RELOAD:.*]] = load i32, ptr %[[COND_PRIV]], align 4
// LLVM: call void @work(ptr noundef %[[A]], i32 noundef %[[COND_RELOAD]])

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_array_section{{.*}}(
// LLVM: %[[SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[SLOT]], align 8
// LLVM: %[[A:.*]] = load ptr, ptr %[[SLOT]], align 8
// LLVM: call void @work(ptr noundef %[[A]], i32 noundef 1)

// OGCG-LABEL: define {{.*}} @target_scalar_if_device(
// OGCG:         call i32 @device()
// OGCG:         call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_scalar_if_device{{.*}}(ptr {{.*}} %a, i64 {{.*}} %cond)
// OGCG:         call void @work(

// OGCG-LABEL: define {{.*}} @target_array_section(
// OGCG:         %.offload_baseptrs = alloca [2 x ptr]
// OGCG:         %.offload_ptrs = alloca [2 x ptr]
// OGCG:         call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_array_section{{.*}}(
// OGCG:         call void @work(
