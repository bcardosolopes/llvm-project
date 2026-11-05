// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-DAG: omp.critical.declare @__omp_critical_the_name hint(contended)
// CHECK: cir.func
void omp_critical_1() {
// CHECK: omp.critical {
// CHECK: cir.call @{{.*}}work
// CHECK: omp.terminator
#pragma omp critical
  work();
}

// CHECK: cir.func
void omp_critical_2() {
// CHECK: omp.critical(@__omp_critical_the_name) {
// CHECK: cir.call @{{.*}}work
// CHECK: omp.terminator
#pragma omp critical(the_name) hint(2)
  work();
}
