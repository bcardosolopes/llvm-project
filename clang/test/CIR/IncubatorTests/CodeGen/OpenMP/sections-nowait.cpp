// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void a();
void b();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}f
void f() {
// CHECK: omp.sections nowait {
// CHECK: omp.section {
// CHECK: cir.call @{{.*}}a
// CHECK: omp.terminator
// CHECK: omp.section {
// CHECK: cir.call @{{.*}}b
// CHECK: omp.terminator
// CHECK: omp.terminator
#pragma omp sections nowait
  {
#pragma omp section
    a();
#pragma omp section
    b();
  }
}
