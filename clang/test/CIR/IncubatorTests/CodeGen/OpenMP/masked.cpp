// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_masked_1
void omp_masked_1() {
// CHECK: omp.masked {
// CHECK: cir.call @{{.*}}work
// CHECK: omp.terminator
#pragma omp masked
  work();
}

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_masked_2
void omp_masked_2(int filter) {
// CHECK: omp.masked filter(%{{.+}} : i32) {
// CHECK: cir.call @{{.*}}work
// CHECK: omp.terminator
#pragma omp masked filter(filter)
  work();
}
