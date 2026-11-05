// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void taskloop_strict_grainsize(int n) {
#pragma omp taskloop grainsize(strict : n)
  for (int i = 0; i < 10; ++i) {
  }
}

void taskloop_simd_strict_num_tasks(int n) {
#pragma omp taskloop simd num_tasks(strict : 4)
  for (int i = 0; i < n; ++i) {
  }
}

// LLVM-LABEL: define dso_local void @taskloop_strict_grainsize(
// LLVM: call void @__kmpc_taskloop_5(
// LLVM-SAME: i32 1, i32 1, i64 %{{.*}}, i32 1, ptr {{.*}})

// LLVM-LABEL: define dso_local void @taskloop_simd_strict_num_tasks(
// LLVM: call void @__kmpc_taskloop_5(
// LLVM-SAME: i32 1, i32 2, i64 4, i32 1, ptr {{.*}})

// OGCG-LABEL: define dso_local void @taskloop_strict_grainsize(
// OGCG: call void @__kmpc_taskloop_5(
// OGCG-SAME: i32 1, i32 1, i64 %{{.*}}, i32 1, ptr {{.*}})

// OGCG-LABEL: define dso_local void @taskloop_simd_strict_num_tasks(
// OGCG: call void @__kmpc_taskloop_5(
// OGCG-SAME: i32 1, i32 2, i64 4, i32 1, ptr {{.*}})
