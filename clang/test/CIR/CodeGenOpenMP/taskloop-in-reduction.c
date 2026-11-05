// RUN: %clang_cc1 -x c -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -x c -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -x c -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void taskloop_in_reduction(int *arr) {
  int sum = 0;
#pragma omp taskgroup task_reduction(+: sum)
  {
#pragma omp taskloop in_reduction(+: sum)
    for (int i = 0; i < 4; ++i)
      sum += arr[i];
  }
}

// CHECK-LABEL: cir.func{{.*}} @taskloop_in_reduction(
// CHECK: omp.taskgroup task_reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[TASKGROUP_SUM:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: omp.taskloop in_reduction(@__omp_reduction_add_s32 %[[TASKGROUP_SUM]] -> %[[TASKLOOP_SUM:arg[0-9]+]] : !cir.ptr<!s32i>)
// CHECK: omp.loop_nest
// CHECK: %[[TASKLOOP_LOAD:.*]] = cir.load align(4) %[[TASKLOOP_SUM]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[TASKLOOP_ADD:.*]] = cir.binop(add, %[[TASKLOOP_LOAD]], %{{.*}}) nsw : !s32i
// CHECK: cir.store align(4) %[[TASKLOOP_ADD]], %[[TASKLOOP_SUM]] : !s32i, !cir.ptr<!s32i>
// CHECK: omp.yield
// CHECK: }
// CHECK: omp.terminator

// LLVM-LABEL: define {{.*}} @taskloop_in_reduction(
// LLVM:         call void @__kmpc_taskgroup(
// LLVM:         call ptr @__kmpc_taskred_init(
// LLVM:         call ptr @__kmpc_omp_task_alloc(
// LLVM:         call void @__kmpc_taskloop(
// LLVM:         call void @__kmpc_end_taskgroup(
// LLVM-LABEL: define internal void @taskloop_in_reduction..omp_par(
// LLVM:         call ptr @__kmpc_task_reduction_get_th_data(

// OGCG-LABEL: define {{.*}} @taskloop_in_reduction(
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_taskloop(
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})

// OGCG-LABEL: define internal i32 @.omp_task_entry.(
// OGCG:         call ptr @__kmpc_task_reduction_get_th_data(
// OGCG:         %add{{.*}} = add nsw i32
