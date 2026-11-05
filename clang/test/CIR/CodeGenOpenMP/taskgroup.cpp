// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

void body(void);

#include <omp.h>

// CHECK-LABEL: omp.declare_reduction @__omp_reduction_add_s32

void taskgroup_clauses(int a) {
  // CHECK-LABEL: cir.func{{.*}}@_Z17taskgroup_clausesi
  // LLVM-LABEL: define {{.*}} void @_Z17taskgroup_clausesi(
#pragma omp taskgroup allocate(omp_pteam_mem_alloc: a) task_reduction(+: a)
  {
    // CHECK: omp.taskgroup allocate(%{{.*}} : !u32i -> %{{.*}} : !cir.ptr<!s32i>) task_reduction(@__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
    // CHECK:   omp.task {
    // CHECK:     cir.call @_Z4bodyv() : () -> ()
    // CHECK:     omp.terminator
    // CHECK:   }
    // CHECK:   omp.terminator
    // CHECK: }
    // LLVM: call void @__kmpc_taskgroup(ptr
    // LLVM-DAG: call ptr @__kmpc_taskred_init(i32 {{.*}}, i32 1, ptr
    // LLVM-DAG: call ptr @__kmpc_omp_task_alloc(ptr
    // LLVM-DAG: call i32 @__kmpc_omp_task(ptr
    // LLVM-DAG: call void @__kmpc_end_taskgroup(ptr
#pragma omp task
    body();
  }
}
