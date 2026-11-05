// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c -fopenmp -I %S/../../Headers/Inputs/include -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

#include <omp.h>

// CHECK-LABEL: omp.declare_reduction @__omp_reduction_add_byref__cir_array_cir_int_s_32_x_4
// CHECK-SAME: : !cir.ptr<!cir.array<!s32i x 4>> attributes {byref_element_type = !cir.array<!s32i x 4>} alloc {
// CHECK: init {
// CHECK: ^bb0(%{{.*}}: !cir.ptr<!cir.array<!s32i x 4>>{{.*}}, %[[INITARG:arg[0-9]+]]: !cir.ptr<!cir.array<!s32i x 4>>{{.*}}):
// CHECK: %[[COUNT:.*]] = cir.const #cir.int<4> : !u64i
// CHECK: %[[BEGIN:.*]] = cir.cast bitcast %[[INITARG]] : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
// CHECK: %[[END:.*]] = cir.ptr_stride inbounds %[[BEGIN]], %[[COUNT]] : (!cir.ptr<!s32i>, !u64i) -> !cir.ptr<!s32i>
// CHECK: cir.do {
// CHECK: %[[CUR:.*]] = cir.load{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CHECK: %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// CHECK: cir.store{{.*}} %[[ZERO]], %[[CUR]] : !s32i, !cir.ptr<!s32i>
// CHECK: combiner {
// CHECK: ^bb0(%[[LHSARG:arg[0-9]+]]: !cir.ptr<!cir.array<!s32i x 4>>{{.*}}, %[[RHSARG:arg[0-9]+]]: !cir.ptr<!cir.array<!s32i x 4>>{{.*}}):
// CHECK: %[[LHSBEGIN:.*]] = cir.cast bitcast %[[LHSARG]] : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
// CHECK: %[[RHSBEGIN:.*]] = cir.cast bitcast %[[RHSARG]] : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
// CHECK: cir.do {
// CHECK: %[[LHSCUR:.*]] = cir.load{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CHECK: %[[RHSCUR:.*]] = cir.load{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CHECK: %[[LHSVAL:.*]] = cir.load{{.*}} %[[LHSCUR]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[RHSVAL:.*]] = cir.load{{.*}} %[[RHSCUR]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[SUM:.*]] = cir.binop(add, %[[LHSVAL]], %[[RHSVAL]]) nsw : !s32i
// CHECK: cir.store{{.*}} %[[SUM]], %[[LHSCUR]] : !s32i, !cir.ptr<!s32i>

void task_fixed_array_in_reduction(void) {
  int a[4];
  // CHECK-LABEL: cir.func{{.*}}@task_fixed_array_in_reduction
#pragma omp taskgroup task_reduction(+: a)
  {
    // CHECK: omp.taskgroup task_reduction(byref @__omp_reduction_add_byref__cir_array_cir_int_s_32_x_4 %{{.*}} -> %[[TGARG:arg[0-9]+]] : !cir.ptr<!cir.array<!s32i x 4>>) {
#pragma omp task in_reduction(+: a)
    {
      // CHECK: omp.task in_reduction(byref @__omp_reduction_add_byref__cir_array_cir_int_s_32_x_4 %[[TGARG]] -> %[[TASKARG:arg[0-9]+]] : !cir.ptr<!cir.array<!s32i x 4>>)
      // CHECK: %[[ONE:.*]] = cir.const #cir.int<1> : !s32i
      // CHECK: %[[IDX:.*]] = cir.const #cir.int<0> : !s32i
      // CHECK: %[[ELT:.*]] = cir.get_element %[[TASKARG]][%[[IDX]] : !s32i] : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
      // CHECK: %[[VAL:.*]] = cir.load{{.*}} %[[ELT]] : !cir.ptr<!s32i>, !s32i
      // CHECK: %[[ADD:.*]] = cir.binop(add, %[[VAL]], %[[ONE]]) nsw : !s32i
      // CHECK: cir.store{{.*}} %[[ADD]], %[[ELT]] : !s32i, !cir.ptr<!s32i>
      a[0] += 1;
    }
  }
}

// OGCG-LABEL: define dso_local void @task_fixed_array_in_reduction()
// OGCG: call void @__kmpc_taskgroup
// OGCG: call ptr @__kmpc_taskred_init
// OGCG: call ptr @__kmpc_omp_task_alloc
// OGCG: call i32 @__kmpc_omp_task
// OGCG: call void @__kmpc_end_taskgroup
