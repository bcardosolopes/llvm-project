// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-llvm -std=c++11 -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp \
// RUN:   -emit-llvm -std=c++11 -o - %s | FileCheck --check-prefix=OGCG %s

// Verify that #pragma omp unroll partial does not crash due to duplicate
// symbol table entries for the loop variable. The unroll directive should
// not pre-emit the loop variable declaration separately from the for-stmt.

extern "C" void body(...);

extern "C" void func(int start, int end, int step) {
  #pragma omp unroll partial(4)
  for (int i = start; i < end; i += step)
    body(start, end, step, i);
}

// CHECK-LABEL: define {{.*}} @func(
// CHECK: alloca i32
// CHECK: br label
// CHECK: icmp slt
// CHECK: br i1

// OGCG-LABEL: define {{.*}} @func(
// OGCG: alloca i32
// OGCG: br label
// OGCG: icmp slt
// OGCG: br i1
