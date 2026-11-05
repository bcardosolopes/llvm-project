// RUN: %clang_cc1 -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);

#include <omp.h>

void task_supported_clauses(int pred, int prio) {
  // CHECK-LABEL: cir.func{{.*}}@_Z22task_supported_clausesii
  // LLVM-LABEL: define{{.*}} @_Z22task_supported_clausesii(
  // LLVM: call {{.*}}@__kmpc_omp_task_alloc(
  // LLVM: call i32 @__kmpc_omp_task(
#pragma omp task if(pred) final(pred) priority(prio) mergeable untied
  {
    // CHECK: omp.task final(%{{.*}}) if(%{{.*}}) mergeable priority(%{{.*}} : i32) untied
    // CHECK:   cir.call @_Z4bodyv() : () -> ()
    // CHECK:   omp.terminator
    // CHECK: }
    body();
  }
}

// OGCG-LABEL: define{{.*}} @_Z22task_supported_clausesii(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task(

void task_depend_simple(int in, int *out, int *inout, int *mutexv, int *inoutsetv) {
  int local = 0;
  (void)out;
  // CHECK-LABEL: cir.func{{.*}}@_Z18task_depend_simpleiPiS_S_S_
#pragma omp task depend(in : in) depend(out : local) depend(inout : *inout) depend(mutexinoutset : *mutexv) depend(inoutset : *inoutsetv)
  {
    // CHECK: omp.task depend(taskdependin -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64), taskdependout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64), taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64), taskdependmutexinoutset -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64), taskdependinoutset -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64)) {
    // CHECK:   cir.call @_Z4bodyv() : () -> ()
    // CHECK:   omp.terminator
    // CHECK: }
    body();
  }
}

// OGCG-LABEL: define{{.*}} @_Z18task_depend_simpleiPiS_S_S_(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task_with_deps(

void task_firstprivate_simple(int x) {
  // CHECK-LABEL: cir.func{{.*}}@_Z24task_firstprivate_simplei
  // LLVM-LABEL: define{{.*}} @_Z24task_firstprivate_simplei(
  // LLVM: call {{.*}}@__kmpc_omp_task_alloc(
  // LLVM: call i32 @__kmpc_omp_task(
#pragma omp task firstprivate(x)
  {
    // CHECK: omp.task private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
    // CHECK:   %[[PRIV:.*]] = cir.load align(4) %{{.*}} : !cir.ptr<!s32i>, !s32i
    // CHECK:   cir.store align(4) %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i>
    // CHECK:   omp.terminator
    // CHECK: }
    x = x + 1;
  }
}

// OGCG-LABEL: define{{.*}} @_Z24task_firstprivate_simplei(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task(

void task_depend_runtime_section(int *p, int i, int n) {
  // CHECK-LABEL: cir.func{{.*}}@_Z{{[0-9]+}}task_depend_runtime_sectionPiii
#pragma omp task depend(inout : p[i:n])
  {
    // CHECK: %[[SIZE:.*]] = llvm.mul %{{.*}}, %{{.*}} : i64
    // CHECK: omp.task depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%[[SIZE]] : i64)) {
    // CHECK:   cir.call @_Z4bodyv() : () -> ()
    // CHECK:   omp.terminator
    // CHECK: }
    body();
  }
}

// OGCG-LABEL: define{{.*}} @_Z27task_depend_runtime_sectionPiii(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task_with_deps(

// Task firstprivate inside a parallel region — the variable is captured
// by the parallel and then firstprivatized by the task.  The task body
// is emitted in __captured_stmt, so we match there.
void task_firstprivate_in_parallel(int x) {
  // CHECK-LABEL: cir.func{{.*}}@__captured_stmt
#pragma omp parallel
  {
#pragma omp single
    {
#pragma omp task firstprivate(x)
      {
        // CHECK: omp.task private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
        // CHECK:   omp.terminator
        // CHECK: }
        x = x + 1;
      }
    }
  }
}

// OGCG-LABEL: define{{.*}} @_Z29task_firstprivate_in_paralleli(
// OGCG: call void {{.*}} @__kmpc_fork_call(

void task_private_simple(int x) {
  // CHECK-LABEL: cir.func{{.*}}@_Z19task_private_simplei
#pragma omp task private(x)
  {
    // CHECK: omp.task private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
    // CHECK:   omp.terminator
    // CHECK: }
    x = x + 1;
  }
}

// OGCG-LABEL: define{{.*}} @_Z19task_private_simplei(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task(

void task_allocate_simple(int x) {
  // CHECK-LABEL: cir.func{{.*}}@_Z20task_allocate_simplei
#pragma omp task firstprivate(x) allocate(omp_pteam_mem_alloc:x)
  {
    // CHECK: %[[ALLOC:.*]] = cir.const #cir.int<7> : !u32i
    // CHECK: omp.task allocate(%[[ALLOC]] : !u32i -> %{{.*}} : !cir.ptr<!s32i>) private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
    // CHECK:   cir.call @_Z4bodyv() : () -> ()
    // CHECK:   omp.terminator
    // CHECK: }
    body();
  }
}

// OGCG-LABEL: define{{.*}} @_Z20task_allocate_simplei(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task(

void task_in_reduction_simple(int a) {
  // CHECK-LABEL: cir.func{{.*}}@_Z24task_in_reduction_simplei
#pragma omp taskgroup task_reduction(+: a)
  {
    // CHECK: omp.taskgroup task_reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[TGARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
#pragma omp task in_reduction(+: a)
    {
      // CHECK: omp.task in_reduction(@__omp_reduction_add_s32 %[[TGARG]] -> %[[TASKARG:arg[0-9]+]] : !cir.ptr<!s32i>)
      // CHECK:   %[[ONE:.*]] = cir.const #cir.int<1> : !s32i
      // CHECK:   %[[VAL:.*]] = cir.load align(4) %[[TASKARG]] : !cir.ptr<!s32i>, !s32i
      // CHECK:   %[[ADD:.*]] = cir.binop(add, %[[VAL]], %[[ONE]]) nsw : !s32i
      // CHECK:   cir.store align(4) %[[ADD]], %[[TASKARG]] : !s32i, !cir.ptr<!s32i>
      // CHECK:   omp.terminator
      // CHECK: }
      a += 1;
    }
    // CHECK: omp.terminator
  }
}

// LLVM-LABEL: define{{.*}} @_Z{{[0-9]+}}task_depend_runtime_sectionPiii(
// LLVM: %[[SIZE:.+]] = mul i64 %{{.+}}, 4
// LLVM: store i64 %[[SIZE]], ptr %{{.+}}, align 8
// LLVM: call i32 @__kmpc_omp_task_with_deps(

// OGCG-LABEL: define{{.*}} @_Z24task_in_reduction_simplei(
// OGCG: call void @__kmpc_taskgroup(
// OGCG: call {{.*}}@__kmpc_taskred_init(
// OGCG: call {{.*}}@__kmpc_omp_task_alloc(
// OGCG: call i32 @__kmpc_omp_task(
// OGCG: call void @__kmpc_end_taskgroup(
