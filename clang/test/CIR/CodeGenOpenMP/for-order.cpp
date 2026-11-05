// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int aaa[50];

void parallel_order_loop() {
#pragma omp parallel
  {
#pragma omp for order(concurrent)
    {
      for (int i = 0; i < 2; ++i) {
#pragma omp loop
        for (int j = 0; j < 50; ++j)
          aaa[j] = j;
      }
    }
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}parallel_order_loopv(
// CIR: omp.parallel
// CIR: omp.wsloop order(concurrent)
// CIR: omp.loop_nest
// CIR: omp.simd
// CIR: omp.loop_nest

// OGCG-LABEL: define {{.*}} @_Z19parallel_order_loopv(
// OGCG: call void {{.*}} @__kmpc_fork_call(

// OGCG-LABEL: define internal {{.*}} @_Z19parallel_order_loopv.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
