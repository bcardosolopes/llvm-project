// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);

void test_single(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_single()
  // CHECK: omp.parallel
  // CHECK: omp.single
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  #pragma omp parallel
  {
    #pragma omp single
    body();
  }
}

void test_single_nowait(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_single_nowait()
  // CHECK: omp.parallel
  // CHECK: omp.single {{.*}}nowait
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  #pragma omp parallel
  {
    #pragma omp single nowait
    body();
  }
}

// LLVM-LABEL: define {{.*}} @test_single()
// LLVM:         call void {{.*}} @__kmpc_fork_call(
// LLVM:       define internal void @test_single..omp_par(
// LLVM-DAG:     call {{.*}} @__kmpc_single(
// LLVM-DAG:     call void @__kmpc_end_single(
// LLVM-DAG:     call void @__kmpc_barrier(

// LLVM-LABEL: define {{.*}} @test_single_nowait()
// LLVM:         call void {{.*}} @__kmpc_fork_call(
// LLVM:       define internal void @test_single_nowait..omp_par(
// LLVM:         call {{.*}} @__kmpc_single(
// LLVM:         call void @__kmpc_end_single(

// OGCG-LABEL: define {{.*}} @test_single()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:       define internal void @test_single.omp_outlined(
// OGCG-DAG:     call {{.*}} @__kmpc_single(
// OGCG-DAG:     call void @__kmpc_end_single(
// OGCG-DAG:     call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @test_single_nowait()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:       define internal void @test_single_nowait.omp_outlined(
// OGCG:         call {{.*}} @__kmpc_single(
// OGCG:         call void @__kmpc_end_single(
