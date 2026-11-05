// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_taskloop_1
void omp_taskloop_1() {
// CHECK: omp.taskloop private({{.*}}) {
// CHECK: omp.loop_nest
// CHECK: cir.call @{{.*}}work
// CHECK: omp.yield
#pragma omp taskloop
  for (int i = 0; i < 8; ++i)
    work();
}
