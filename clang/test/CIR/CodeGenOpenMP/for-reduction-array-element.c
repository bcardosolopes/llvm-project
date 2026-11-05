// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

int g_arr[10];

void test_for_array_element_reduction(int n) {
#pragma omp for reduction(+:g_arr[1])
  for (int i = 0; i < n; i++)
    g_arr[1] += i;
}

// CIR-LABEL: cir.func{{.*}} @test_for_array_element_reduction(
// CIR: %[[IDX:.*]] = cir.const #cir.int<1> : !s32i
// CIR: %[[GLOBAL:.*]] = cir.get_global @g_arr
// CIR: %[[ELEM:.*]] = cir.get_element %[[GLOBAL]][%[[IDX]] : !s32i]
// CIR: omp.wsloop reduction(@__omp_reduction_add_s32 %[[ELEM]] -> %arg{{.*}} : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest

// LLVM-LABEL: define dso_local void @test_for_array_element_reduction(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call i32 @__kmpc_reduce(

void test_for_multi_array_reduction(int n) {
#pragma omp for reduction(+:g_arr[0]) reduction(*:g_arr[2])
  for (int i = 0; i < n; i++) {
    g_arr[0] += i;
    g_arr[2] *= i;
  }
}

// CIR-LABEL: cir.func{{.*}} @test_for_multi_array_reduction(
// CIR: omp.wsloop reduction(@__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}}, @__omp_reduction_mul_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) {
// CIR: omp.loop_nest

// LLVM-LABEL: define dso_local void @test_for_multi_array_reduction(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call i32 @__kmpc_reduce(
