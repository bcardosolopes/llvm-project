// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();
void after();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_for_simd_1
void omp_for_simd_1() {
// CHECK: omp.parallel {
// CHECK: cir.scope {
// CHECK: omp.wsloop {
// CHECK: omp.simd {
// CHECK: omp.loop_nest
// CHECK: cir.call @{{.*}}work
// CHECK: omp.yield
// CHECK: } {omp.composite}
// CHECK: } {omp.composite}
// CHECK: cir.call @{{.*}}after
#pragma omp parallel
  {
#pragma omp for simd
    for (int i = 0; i < 8; ++i)
      work();
    after();
  }
}

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_for_simd_nowait_1
void omp_for_simd_nowait_1() {
// CHECK: omp.parallel {
// CHECK: cir.scope {
// CHECK: omp.wsloop nowait {
// CHECK: omp.simd {
// CHECK: omp.loop_nest
// CHECK: cir.call @{{.*}}work
// CHECK: omp.yield
// CHECK: } {omp.composite}
// CHECK: } {omp.composite}
// CHECK: cir.call @{{.*}}after
#pragma omp parallel
  {
#pragma omp for simd nowait
    for (int i = 0; i < 8; ++i)
      work();
    after();
  }
}
