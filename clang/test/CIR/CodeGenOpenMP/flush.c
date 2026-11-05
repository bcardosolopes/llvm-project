// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void test_flush(void) {
  int a = 0;
  // CHECK-LABEL: cir.func{{.*}} @test_flush()
  // CHECK: omp.flush
  // CHECK: omp.flush
  #pragma omp flush
  #pragma omp flush(a)
}

void test_flush_orderings(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_flush_orderings()
  // CHECK: omp.flush
  // CHECK: omp.flush
  // CHECK: omp.flush
  // CHECK: omp.flush
  #pragma omp flush seq_cst
  #pragma omp flush acq_rel
  #pragma omp flush acquire
  #pragma omp flush release
}

// LLVM-LABEL: define {{.*}} @test_flush()
// LLVM:         call void @__kmpc_flush(
// LLVM:         call void @__kmpc_flush(
// LLVM-LABEL: define {{.*}} @test_flush_orderings()
// LLVM:         call void @__kmpc_flush(
// LLVM:         call void @__kmpc_flush(
// LLVM:         call void @__kmpc_flush(
// LLVM:         call void @__kmpc_flush(

// OGCG-LABEL: define {{.*}} @test_flush()
// OGCG:         call void @__kmpc_flush(
// OGCG:         call void @__kmpc_flush(
// OGCG-LABEL: define {{.*}} @test_flush_orderings()
// OGCG:         call void @__kmpc_flush(
// OGCG:         call void @__kmpc_flush(
// OGCG:         call void @__kmpc_flush(
// OGCG:         call void @__kmpc_flush(
