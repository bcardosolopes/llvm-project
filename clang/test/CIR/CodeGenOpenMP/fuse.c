// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(int);

void fuse_constant_loop_sequence(void) {
  // CHECK: cir.func{{.*}} @fuse_constant_loop_sequence
#pragma omp fuse
  {
    for (int i = 0; i < 4; ++i)
      body(i);
    for (int j = 10; j < 14; ++j)
      body(j);
  }
  // CHECK: cir.for
  // CHECK: cir.call @body
  // CHECK: cir.call @body
}

// LLVM-LABEL: define {{.*}} @fuse_constant_loop_sequence()
// LLVM:         call void @body(

// OGCG-LABEL: define {{.*}} @fuse_constant_loop_sequence()
// OGCG:         call void @body(
