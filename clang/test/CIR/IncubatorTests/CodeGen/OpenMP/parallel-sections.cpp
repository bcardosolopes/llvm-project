// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void foo();
void bar();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}f
void f() {
// CHECK: omp.parallel {
// CHECK: omp.sections nowait {
// CHECK: omp.section {
// CHECK: cir.call @{{.*}}foo
// CHECK: omp.section {
// CHECK: cir.call @{{.*}}bar
#pragma omp parallel sections
  {
#pragma omp section
    foo();
#pragma omp section
    bar();
  }
}
