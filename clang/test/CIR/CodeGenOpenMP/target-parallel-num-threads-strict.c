// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void target_parallel_num_threads_strict(int n) {
#pragma omp target parallel num_threads(strict : 32 + n) severity(warning)
  {
  }
}

// CIR-LABEL: cir.func{{.*}} @target_parallel_num_threads_strict(
// CIR: omp.target host_eval(%{{.*}} -> %{{.*}} : i32) {
// CIR: omp.parallel num_threads(strict: %{{.*}} : i32) severity(warning)

// LLVM-LABEL: define dso_local void @target_parallel_num_threads_strict(
// LLVM: %{{.*}} = add nsw i32 32, %{{.*}}
// LLVM: call i32 @__tgt_target_kernel(ptr @{{.*}}, i64 -1, i32 1, i32 %{{.*}}, ptr @{{.*}}, ptr %{{.*}})
// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}target_parallel_num_threads_strict_l7(i32
// LLVM: call void @__kmpc_push_num_threads_strict(ptr @{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 1, ptr null)

// OGCG-LABEL: define dso_local void @target_parallel_num_threads_strict(
// OGCG:         call void @__omp_offloading_{{.*}}target_parallel_num_threads_strict
// OGCG:         define internal void @__omp_offloading_{{.*}}target_parallel_num_threads_strict
// OGCG:         call void @__kmpc_push_num_threads_strict(ptr @{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 1, ptr null)
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
