// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void simd_collapse_loop(float *a, float *b) {
#pragma omp simd collapse(2)
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j)
      a[i + j] = b[i + j];
}

void simd_collapse_outer_loop(float *a, float *b) {
  int i;
  short j;
#pragma omp simd collapse(2)
  for (i = 1; i < 3; ++i)
    for (j = 2; j < 5; ++j)
      a[i + j] = b[i + j];
}

// CIR-LABEL: cir.func{{.*}} @{{.*}}simd_collapse_loop{{.*}}(
// CIR: %[[I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CIR: %[[J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["j"]
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.store align(4) %{{.*}}, %[[I]]
// CIR: cir.store align(4) %{{.*}}, %[[J]]

// CIR-LABEL: cir.func{{.*}} @{{.*}}simd_collapse_outer_loop{{.*}}(
// CIR: %[[I_OUTER:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CIR: %[[J_OUTER:.*]] = cir.alloca !s16i, !cir.ptr<!s16i>, ["j"]
// CIR: %[[OMP_IV:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, [".omp.iv"]
// CIR: %[[I_INNER:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CIR: %[[J_INNER:.*]] = cir.alloca !s16i, !cir.ptr<!s16i>, ["j"]
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.store align(4) %{{.*}}, %[[I_INNER]]
// CIR: cir.store align(2) %{{.*}}, %[[J_INNER]]
// CIR: cir.store align(4) %{{.*}}, %[[I_OUTER]]
// CIR: cir.store align(2) %{{.*}}, %[[J_OUTER]]

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}simd_collapse_loopPfS_(
// OGCG: br label %omp.inner.for.cond

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}simd_collapse_outer_loopPfS_(
// OGCG: br label %omp.inner.for.cond
