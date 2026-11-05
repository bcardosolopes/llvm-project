// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_loop_num_threads_strict(void) {
#pragma omp parallel loop num_threads(strict : 4) severity(fatal) message("msg")
  for (int i = 0; i < 4; ++i) {
  }
}

// CIR-LABEL: cir.func{{.*}} @parallel_loop_num_threads_strict(
// CIR: omp.parallel num_threads(strict: %{{.*}} : i32) severity(fatal) message(%{{.*}} : !cir.ptr<!s8i>)
// CIR: omp.wsloop

// LLVM-LABEL: define{{.*}} @parallel_loop_num_threads_strict
// LLVM: call void @__kmpc_push_num_threads_strict(
// LLVM-SAME: i32 4, i32 2,

// OGCG-LABEL: define{{.*}} @parallel_loop_num_threads_strict
// OGCG: call void @__kmpc_push_num_threads_strict(
// OGCG-SAME: i32 4, i32 2,
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
