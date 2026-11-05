// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=PLAIN --check-prefix=HOST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=PLAIN --check-prefix=TARGET
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_loop_order(int *p) {
#pragma omp parallel loop order(concurrent)
  for (int i = 0; i < 4; ++i)
    p[i] = i;
}

void target_parallel_loop_order(int *p) {
#pragma omp target parallel loop map(tofrom : p[0:4]) order(concurrent)
  for (int i = 0; i < 4; ++i)
    p[i] = i;
}

// PLAIN-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_orderPi(
// PLAIN: omp.parallel
// PLAIN: omp.wsloop nowait order(concurrent)
// PLAIN: omp.loop_nest

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_orderPi(
// HOST: omp.parallel
// HOST: omp.wsloop nowait order(concurrent)
// HOST: omp.loop_nest

// TARGET-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_parallel_loop_orderPi(
// TARGET: omp.target
// TARGET: omp.parallel
// TARGET: omp.wsloop nowait order(concurrent)
// TARGET: omp.loop_nest

// LLVM: !llvm.access.group
// LLVM-COUNT-2: !{!"llvm.loop.parallel_accesses"

// OGCG-LABEL: define dso_local void @_Z19parallel_loop_orderPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z19parallel_loop_orderPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
