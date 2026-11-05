// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// CHECK: cir.func
void omp_cancel_1(bool flag) {
// CHECK: omp.parallel {
// CHECK: omp.cancellation_point cancellation_construct_type(parallel)
// CHECK: omp.cancel cancellation_construct_type(parallel) if(%{{.*}})
// CHECK: omp.terminator
#pragma omp parallel
  {
#pragma omp cancellation point parallel
#pragma omp cancel parallel if(flag)
  }
}
