// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void loop_bind_parallel_orphan(int *p) {
#pragma omp loop bind(parallel) order(concurrent)
  for (int i = 0; i < 4; ++i)
    p[i] = i;
}

void loop_implicit_bind_parallel(int *p, int n) {
  int j;
#pragma omp parallel
  {
#pragma omp loop
    for (j = 0; j < n; ++j)
      p[j] = j;
  }
}

void loop_bind_thread_orphan(int *out, int n) {
  int j;
  int sum = 0;
#pragma omp loop collapse(2) reduction(+ : sum) lastprivate(j) bind(thread)
  for (int i = 0; i < n; ++i)
    for (j = 0; j < n; ++j)
      sum += i + j;
  *out = sum + j;
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}loop_bind_parallel_orphanPi(
// CIR-NOT: omp.parallel
// CIR: omp.wsloop order(concurrent)
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}loop_implicit_bind_parallelPii(
// CIR: omp.parallel
// CIR: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}loop_bind_thread_orphanPii(
// CIR-NOT: omp.wsloop
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.if

// OGCG-LABEL: define dso_local void @_Z25loop_bind_parallel_orphanPi(
// OGCG-NOT: @__kmpc_fork_call
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z27loop_implicit_bind_parallelPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z27loop_implicit_bind_parallelPii.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z23loop_bind_thread_orphanPii(
// OGCG-NOT: @__kmpc_for_static_init
