// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_parallel_master_1
void omp_parallel_master_1() {
// CHECK: omp.parallel {
// CHECK: cir.scope {
// CHECK: omp.master {
// CHECK: cir.call @{{.*}}work
// CHECK: omp.terminator
// CHECK: omp.terminator
#pragma omp parallel master
  work();
}
