// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fopenmp-enable-irbuilder -fopenmp -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void work();

// CHECK-LABEL: cir.func no_inline optnone internal private @__captured_stmt
// CHECK: cir.call @{{.*}}work

// CHECK-LABEL: cir.func no_inline optnone dso_local @{{.*}}omp_single_1
void omp_single_1() {
// CHECK: omp.single copyprivate() {
// CHECK: cir.call @__captured_stmt
// CHECK: omp.terminator
#pragma omp single
  work();
}
