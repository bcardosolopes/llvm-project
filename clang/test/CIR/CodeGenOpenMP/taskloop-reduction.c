// RUN: %clang_cc1 -x c -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -x c -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -x c -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void taskloop_reduction(int *arr, int n) {
  int sum = 0;
#pragma omp taskloop reduction(+: sum)
  for (int i = 0; i < n; ++i)
    sum += arr[i];
}

// CHECK-LABEL: cir.func{{.*}} @taskloop_reduction(
// CHECK: omp.taskloop
// CHECK-SAME: reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[TASKLOOP_SUM:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: omp.loop_nest
// CHECK: %[[TASKLOOP_LOAD:.*]] = cir.load align(4) %[[TASKLOOP_SUM]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[TASKLOOP_ADD:.*]] = cir.binop(add, %[[TASKLOOP_LOAD]], %{{.*}}) nsw : !s32i
// CHECK: cir.store align(4) %[[TASKLOOP_ADD]], %[[TASKLOOP_SUM]] : !s32i, !cir.ptr<!s32i>
// CHECK: omp.yield

void taskloop_simd_reduction(int *arr, int n) {
  int sum = 0;
#pragma omp taskloop simd reduction(+: sum)
  for (int i = 0; i < n; ++i)
    sum += arr[i];
}

// CHECK-LABEL: cir.func{{.*}} @taskloop_simd_reduction(
// CHECK: omp.taskloop
// CHECK-SAME: reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[TASKLOOP_SIMD_SUM:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: omp.simd reduction(@__omp_reduction_add_s32 %[[TASKLOOP_SIMD_SUM]] -> %[[TASKLOOP_SIMD_INNER:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: omp.loop_nest
// CHECK: %[[TASKLOOP_SIMD_LOAD:.*]] = cir.load align(4) %[[TASKLOOP_SIMD_INNER]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[TASKLOOP_SIMD_ADD:.*]] = cir.binop(add, %[[TASKLOOP_SIMD_LOAD]], %{{.*}}) nsw : !s32i
// CHECK: cir.store align(4) %[[TASKLOOP_SIMD_ADD]], %[[TASKLOOP_SIMD_INNER]] : !s32i, !cir.ptr<!s32i>
// CHECK: omp.yield
// CHECK: } {omp.composite}
// CHECK: } {omp.composite}

void master_taskloop_reduction(int *arr, int n) {
  int sum = 0;
#pragma omp master taskloop reduction(+: sum)
  for (int i = 0; i < n; ++i)
    sum += arr[i];
}

// CHECK-LABEL: cir.func{{.*}} @master_taskloop_reduction(
// CHECK: omp.master {
// CHECK: omp.taskloop
// CHECK-SAME: reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[MASTER_TASKLOOP_SUM:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: omp.loop_nest
// CHECK: %[[MASTER_TASKLOOP_LOAD:.*]] = cir.load align(4) %[[MASTER_TASKLOOP_SUM]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[MASTER_TASKLOOP_ADD:.*]] = cir.binop(add, %[[MASTER_TASKLOOP_LOAD]], %{{.*}}) nsw : !s32i
// CHECK: cir.store align(4) %[[MASTER_TASKLOOP_ADD]], %[[MASTER_TASKLOOP_SUM]] : !s32i, !cir.ptr<!s32i>
// CHECK: omp.yield
// CHECK: omp.terminator

void parallel_master_taskloop_reduction(int *arr, int n) {
  int sum = 0;
#pragma omp parallel master taskloop reduction(+: sum)
  for (int i = 0; i < n; ++i)
    sum += arr[i];
}

// CHECK-LABEL: cir.func{{.*}} @parallel_master_taskloop_reduction(
// CHECK: omp.parallel {
// CHECK: cir.scope {
// CHECK: omp.master {
// CHECK: omp.taskloop
// CHECK-SAME: reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[PAR_MASTER_TASKLOOP_SUM:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: omp.loop_nest
// CHECK: %[[PAR_MASTER_TASKLOOP_LOAD:.*]] = cir.load align(4) %[[PAR_MASTER_TASKLOOP_SUM]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[PAR_MASTER_TASKLOOP_ADD:.*]] = cir.binop(add, %[[PAR_MASTER_TASKLOOP_LOAD]], %{{.*}}) nsw : !s32i
// CHECK: cir.store align(4) %[[PAR_MASTER_TASKLOOP_ADD]], %[[PAR_MASTER_TASKLOOP_SUM]] : !s32i, !cir.ptr<!s32i>
// CHECK: omp.yield
// CHECK: omp.terminator
// CHECK: omp.terminator

// LLVM-LABEL: define {{.*}} @taskloop_reduction(
// LLVM:         call void @__kmpc_taskgroup(
// LLVM:         call ptr @__kmpc_taskred_init(
// LLVM:         call ptr @__kmpc_omp_task_alloc(
// LLVM:         call void @__kmpc_taskloop(
// LLVM:         call void @__kmpc_end_taskgroup(
// The per-task private reduction copy must be looked up with the id of the
// thread that executes the task, computed by a __kmpc_global_thread_num call
// inside the outlined task body -- not an id captured from the enclosing
// (task-creating) scope and threaded through the task context.
// LLVM-LABEL: define internal void @taskloop_reduction..omp_par(
// LLVM:         %[[GTID:.*]] = call i32 @__kmpc_global_thread_num(
// LLVM:         call ptr @__kmpc_task_reduction_get_th_data(i32 %[[GTID]],
// LLVM-LABEL: define {{.*}} @taskloop_simd_reduction(
// LLVM:         call void @__kmpc_taskgroup(
// LLVM:         call ptr @__kmpc_taskred_init(
// LLVM:         call ptr @__kmpc_omp_task_alloc(
// LLVM:         call void @__kmpc_taskloop(
// LLVM:         call void @__kmpc_end_taskgroup(
// LLVM-LABEL: define {{.*}} @master_taskloop_reduction(
// LLVM:         call {{.*}} @__kmpc_master(
// LLVM:         call void @__kmpc_end_master(
// LLVM:         call void @__kmpc_taskgroup(
// LLVM:         call ptr @__kmpc_taskred_init(
// LLVM:         call ptr @__kmpc_omp_task_alloc(
// LLVM:         call void @__kmpc_taskloop(
// LLVM:         call void @__kmpc_end_taskgroup(
// LLVM-LABEL: define {{.*}} @parallel_master_taskloop_reduction(
// LLVM:         call void {{.*}} @__kmpc_fork_call(

// OGCG-LABEL: define {{.*}} @taskloop_reduction(
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_taskloop(
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})

// OGCG-LABEL: define internal i32 @.omp_task_entry.(
// OGCG:         call ptr @__kmpc_task_reduction_get_th_data(
// OGCG:         %add{{.*}} = add nsw i32

// OGCG-LABEL: define {{.*}} @taskloop_simd_reduction(
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_taskloop(
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})

// OGCG-LABEL: define {{.*}} @master_taskloop_reduction(
// OGCG:         %{{.*}} = call i32 @__kmpc_master(
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_taskloop(
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call void @__kmpc_end_master(

// OGCG-LABEL: define {{.*}} @parallel_master_taskloop_reduction(
// OGCG:         call void {{.*}} @__kmpc_fork_call(
