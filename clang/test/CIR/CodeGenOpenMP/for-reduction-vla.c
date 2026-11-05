// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify that VLA (variably-length array) reductions compile through CIR.
void test_for_vla_reduction(int n) {
  int arr[n];
  for (int i = 0; i < n; i++)
    arr[i] = 0;
#pragma omp parallel
#pragma omp for reduction(+:arr)
  for (int i = 0; i < n; i++)
    arr[i] += i;
}

// CIR-DAG: cir.global "private"  internal  @[[SIZE_SYM:__omp_task_reduction_size_arr_[0-9]+_[0-9]+]] = #cir.int<0> : !u64i
// CIR-DAG: omp.declare_reduction @[[RED_SYM:__omp_reduction_add_byref_s32_dynamic_arr_[0-9]+_[0-9]+]] : !cir.ptr<!s32i> attributes {byref_element_type = !s32i, omp.frontend.task_reduction_size_sym = @[[SIZE_SYM]]}
// CIR-LABEL: cir.func{{.*}} @test_for_vla_reduction(
// CIR: omp.parallel {
// CIR: omp.wsloop reduction(byref @[[RED_SYM]] %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest

// LLVM-LABEL: define dso_local void @test_for_vla_reduction(
// LLVM: %structArg = alloca { ptr, ptr }, align 8
// LLVM-LABEL: define internal void @test_for_vla_reduction..omp_par(
// LLVM-SAME: i64 %0, ptr noundef nonnull align 8 dereferenceable(16) %1
// LLVM: %gep_ = getelementptr { ptr, ptr }, ptr %1, i32 0, i32 0
// LLVM: %loadgep_ = load ptr, ptr %gep_, align 8
// LLVM: %gep_1 = getelementptr { ptr, ptr }, ptr %1, i32 0, i32 1
// LLVM: %loadgep_2 = load ptr, ptr %gep_1, align 8
// LLVM-NOT: getelementptr { ptr, ptr, ptr }, ptr %1, i32 0, i32 2
// LLVM: %[[IV_SLOT:[^ ]+]] = alloca i32, i64 1, align 4
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call i32 @__kmpc_reduce(
// LLVM-LABEL: omp.loop_nest.region:
// LLVM: store i32 %{{[0-9]+}}, ptr %[[IV_SLOT]], align 4
// LLVM: %{{[0-9]+}} = load i32, ptr %[[IV_SLOT]], align 4
// LLVM: %{{[0-9]+}} = load i32, ptr %[[IV_SLOT]], align 4

// OGCG-LABEL: define dso_local void @test_for_vla_reduction(
// OGCG-LABEL: define internal void @test_for_vla_reduction.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
// OGCG: call i32 @__kmpc_reduce(
// OGCG: call void @__kmpc_end_reduce(
// OGCG: call void @__kmpc_end_reduce(
// OGCG: call void @__kmpc_barrier(
