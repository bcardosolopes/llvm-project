// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_for_simd_collapse_loop(float *a, float *b) {
#pragma omp parallel for simd collapse(2)
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j)
      a[i + j] = b[i + j];
}

void parallel_for_simd_collapse_outer_loop(float *a, float *b) {
  int i;
  short j;
#pragma omp parallel for simd collapse(2)
  for (i = 1; i < 3; ++i)
    for (j = 2; j < 5; ++j)
      a[i + j] = b[i + j];
}

// CIR-LABEL: cir.func{{.*}} @{{.*}}parallel_for_simd_collapse_loop{{.*}}(
// CIR: omp.parallel
// CIR: %[[I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CIR: %[[J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["j"]
// CIR: omp.wsloop nowait
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.store align(4) %{{.*}}, %[[I]]
// CIR: cir.store align(4) %{{.*}}, %[[J]]

// CIR-LABEL: cir.func{{.*}} @{{.*}}parallel_for_simd_collapse_outer_loop{{.*}}(
// CIR: %[[I_OUTER:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CIR: %[[J_OUTER:.*]] = cir.alloca !s16i, !cir.ptr<!s16i>, ["j"]
// CIR: omp.parallel
// CIR: %[[OMP_IV:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, [".omp.iv"]
// CIR: %[[I_INNER:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CIR: %[[J_INNER:.*]] = cir.alloca !s16i, !cir.ptr<!s16i>, ["j"]
// CIR: omp.wsloop nowait
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.store align(4) %{{.*}}, %[[I_INNER]]
// CIR: cir.store align(2) %{{.*}}, %[[J_INNER]]
// CIR: cir.store align(4) %{{.*}}, %[[I_OUTER]]
// CIR: cir.store align(2) %{{.*}}, %[[J_OUTER]]

// OGCG-LABEL: define dso_local void @_Z31parallel_for_simd_collapse_loopPfS_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z31parallel_for_simd_collapse_loopPfS_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z37parallel_for_simd_collapse_outer_loopPfS_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z37parallel_for_simd_collapse_outer_loopPfS_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
