// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_loop_basic(int *p, int n) {
#pragma omp parallel loop if(parallel : n) num_threads(n) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void parallel_loop_bind_parallel(int *p, int n) {
#pragma omp parallel loop bind(parallel) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void parallel_loop_bind_thread(int *p, int n) {
#pragma omp parallel loop bind(thread) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void parallel_loop_bind_teams(int *p, int n) {
#pragma omp parallel loop bind(teams) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void parallel_loop_collapse_reduction(int *p) {
  int sum = 0;
#pragma omp parallel loop collapse(2) reduction(+ : sum)
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      sum += i + j;
  p[0] = sum;
}

void parallel_loop_mixed_bound_types(int *p, long *limit, int n) {
#pragma omp parallel loop firstprivate(n)
  for (int i = 0; i < *limit; ++i)
    p[0] += i + n;
}

void parallel_loop_collapse_lastprivate_use_after(int *out, int n) {
  int j;
#pragma omp parallel loop collapse(2) lastprivate(j)
  for (int i = 0; i < n; ++i)
    for (j = 0; j < n; ++j) {
    }
  *out = j;
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_basicPii(
// CIR: omp.parallel if(%{{.*}}) num_threads(%{{.*}} : i32)
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_bind_parallelPii(
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_bind_threadPii(
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR-NOT: omp.simd
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_bind_teamsPii(
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_collapse_reductionPi(
// CIR: omp.parallel
// CIR: omp.wsloop nowait reduction(
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_mixed_bound_typesPiPli(
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_loop_collapse_lastprivate_use_afterPii(
// CIR: %[[J_OUTER:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["j"]
// CIR: omp.parallel
// CIR: omp.wsloop nowait
// CIR: %[[J_VAL:.*]] = cir.load align(4) %[[J_OUTER]] : !cir.ptr<!s32i>, !s32i

// OGCG-LABEL: define dso_local void @_Z19parallel_loop_basicPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z19parallel_loop_basicPii.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z27parallel_loop_bind_parallelPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z27parallel_loop_bind_parallelPii.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @_Z25parallel_loop_bind_threadPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// OGCG-LABEL: define dso_local void @_Z24parallel_loop_bind_teamsPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// OGCG-LABEL: define dso_local void @_Z32parallel_loop_collapse_reductionPi(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// OGCG-LABEL: define dso_local void @_Z31parallel_loop_mixed_bound_typesPiPli(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// OGCG-LABEL: define dso_local void @_Z44parallel_loop_collapse_lastprivate_use_afterPii(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
