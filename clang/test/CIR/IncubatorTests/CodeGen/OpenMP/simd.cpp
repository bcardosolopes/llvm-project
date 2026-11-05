// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}f
void f() {
// CHECK: omp.simd {
// CHECK: omp.loop_nest
// CHECK: omp.ordered.region par_level_simd {
// CHECK: cir.call @{{.*}}work
// CHECK: omp.yield
#pragma omp simd
  for (int i = 0; i < 4; ++i) {
#pragma omp ordered simd
    work();
  }
}
