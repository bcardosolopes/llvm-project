// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);

// The combined `parallel masked` / `parallel master` directives lower to an
// `omp.parallel` region wrapping an `omp.masked` / `omp.master` region; the
// parallel-related clauses (e.g. num_threads) drive the parallel op and
// `filter` drives the masked op.

void test_parallel_masked(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_parallel_masked()
  // CHECK: omp.parallel num_threads
  // CHECK: omp.masked {
  // CHECK: cir.call @body()
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  #pragma omp parallel masked num_threads(4)
  body();
}

void test_parallel_masked_filter(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_parallel_masked_filter()
  // CHECK: omp.parallel num_threads
  // CHECK: omp.masked filter(%{{.*}}
  // CHECK: cir.call @body()
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  #pragma omp parallel masked filter(1) num_threads(4)
  body();
}

void test_parallel_master(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_parallel_master()
  // CHECK: omp.parallel num_threads
  // CHECK: omp.master {
  // CHECK: cir.call @body()
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  #pragma omp parallel master num_threads(4)
  body();
}

// LLVM-LABEL: define {{.*}} @test_parallel_masked()
// LLVM:         call void {{.*}} @__kmpc_fork_call(
// LLVM:       define internal void @test_parallel_masked..omp_par(
// LLVM:         call {{.*}} @__kmpc_masked(
// LLVM:         call void @body()
// LLVM:         call void @__kmpc_end_masked(

// LLVM-LABEL: define {{.*}} @test_parallel_master()
// LLVM:         call void {{.*}} @__kmpc_fork_call(
// LLVM:       define internal void @test_parallel_master..omp_par(
// LLVM:         call {{.*}} @__kmpc_master(
// LLVM:         call void @body()
// LLVM:         call void @__kmpc_end_master(

// OGCG-LABEL: define {{.*}} @test_parallel_masked()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:       define internal void @test_parallel_masked.omp_outlined(
// OGCG:         call {{.*}} @__kmpc_masked(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_end_masked(

// OGCG-LABEL: define {{.*}} @test_parallel_master()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:       define internal void @test_parallel_master.omp_outlined(
// OGCG:         call {{.*}} @__kmpc_master(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_end_master(
