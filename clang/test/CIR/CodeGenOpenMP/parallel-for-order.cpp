// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm -O2 %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_for_order(int *p) {
#pragma omp parallel for order(concurrent)
  for (int i = 0; i < 4; ++i)
    p[i] = i;
}

// CIR-LABEL: cir.func{{.*}} @_Z18parallel_for_orderPi(
// CIR: omp.parallel
// CIR: omp.wsloop nowait order(concurrent)
// CIR: omp.loop_nest

// order(concurrent) permits the loop body accesses to run concurrently, so the
// vectorizer is allowed to mark them parallel.
// LLVM: !llvm.access.group
// LLVM: !{!"llvm.loop.parallel_accesses"

// OGCG-LABEL: define dso_local void @_Z18parallel_for_orderPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z18parallel_for_orderPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
