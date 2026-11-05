// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);

void test_master(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_master()
  // CHECK: omp.parallel
  // CHECK: omp.master
  // CHECK: cir.call @body()
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  #pragma omp parallel
  {
    #pragma omp master
    body();
  }
}

// LLVM-LABEL: define {{.*}} @test_master()
// LLVM:         call void {{.*}} @__kmpc_fork_call(
// LLVM:       define internal void @test_master..omp_par(
// LLVM:         call {{.*}} @__kmpc_master(
// LLVM:         call void @body()
// LLVM:         call void @__kmpc_end_master(

// OGCG-LABEL: define {{.*}} @test_master()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:       define internal void @test_master.omp_outlined(
// OGCG:         call {{.*}} @__kmpc_master(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_end_master(
