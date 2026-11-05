// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fopenmp-targets=i386-pc-linux-gnu -x c++ -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fopenmp-targets=i386-pc-linux-gnu -x c++ -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void explicit_firstprivate_scalar() {
  int x = 1;
#pragma omp target firstprivate(x)
  x += 1;
}

void explicit_firstprivate_pointer() {
  int *p = 0;
#pragma omp target firstprivate(p)
  if (p)
    p[0] = 1;
}

void implicit_pointer_capture() {
  int *p = 0;
#pragma omp target
  if (p)
    p[0] = 1;
}

// CHECK-DAG: @.offload_sizes{{.*}} = private unnamed_addr constant [1 x i64] [i64 4]
// CHECK-DAG: @.offload_sizes{{.*}} = private unnamed_addr constant [1 x i64] zeroinitializer
// CHECK-DAG: @.offload_sizes{{.*}} = private unnamed_addr constant [1 x i64] zeroinitializer
// CHECK-DAG: @.offload_maptypes{{.*}} = private unnamed_addr constant [1 x i64] [i64 288]
// CHECK-DAG: @.offload_maptypes{{.*}} = private unnamed_addr constant [1 x i64] [i64 288]
// CHECK-DAG: @.offload_maptypes{{.*}} = private unnamed_addr constant [1 x i64] [i64 544]

// OGCG-LABEL: @_Z28explicit_firstprivate_scalarv(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z29explicit_firstprivate_pointerv(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z24implicit_pointer_capturev(
// OGCG: call i32 @__tgt_target_kernel(
