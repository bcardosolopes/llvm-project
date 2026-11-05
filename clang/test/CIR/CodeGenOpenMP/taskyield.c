// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void test_taskyield(void) {
  // CHECK-LABEL: cir.func{{.*}} @test_taskyield()
  // CHECK: omp.parallel
  // CHECK: omp.taskyield
  // CHECK: omp.terminator
  #pragma omp parallel
  {
    #pragma omp taskyield
  }
}

// LLVM-LABEL: define {{.*}} @test_taskyield()
// LLVM:         call void {{.*}} @__kmpc_fork_call(
// LLVM:       define internal void @test_taskyield..omp_par(
// LLVM:         call {{.*}} @__kmpc_omp_taskyield(

// OGCG-LABEL: define {{.*}} @test_taskyield()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:       define internal void @test_taskyield.omp_outlined(
// OGCG:         call {{.*}} @__kmpc_omp_taskyield(
