// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);

void taskgroup_task_reduction_scalar(int x) {
  // CIR-LABEL: cir.func{{.*}} @taskgroup_task_reduction_scalar(
#pragma omp taskgroup task_reduction(+: x)
  {
    // CIR: omp.taskgroup task_reduction(@__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
#pragma omp task
    body();
  }
}

// LLVM-LABEL: define {{.*}} void @taskgroup_task_reduction_scalar(
// LLVM: call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// LLVM: call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// LLVM: call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})

void taskgroup_task_reduction_array(void) {
  int x[4];
  // CIR-LABEL: cir.func{{.*}} @taskgroup_task_reduction_array(
#pragma omp taskgroup task_reduction(+: x)
  {
    // CIR: omp.taskgroup task_reduction(byref @__omp_reduction_add_byref__cir_array_cir_int_s_32_x_4 %{{.*}} -> %arg{{.*}} : !cir.ptr<!cir.array<!s32i x 4>>) {
#pragma omp task
    body();
  }
}

// LLVM-LABEL: define {{.*}} void @taskgroup_task_reduction_array(
// LLVM: call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// LLVM: store i64 16, ptr %{{.*}}, align 8
// LLVM: call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// LLVM: call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})

// OGCG-LABEL: define {{.*}} @taskgroup_task_reduction_scalar(
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call i32 @__kmpc_omp_task(
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})

// OGCG-LABEL: define internal void @.red_init.(
// OGCG:         store i32 0, ptr %{{.*}}

// OGCG-LABEL: define internal void @.red_comb.(
// OGCG:         %add = add nsw i32
// OGCG:         store i32 %add, ptr %{{.*}}

// OGCG-LABEL: define internal i32 @.omp_task_entry.(
// OGCG:         call void @body()

// OGCG-LABEL: define {{.*}} @taskgroup_task_reduction_array(
// OGCG:         call void @__kmpc_taskgroup(ptr @{{.*}}, i32 %{{.*}})
// OGCG:         store i64 16, ptr %{{.*}}
// OGCG:         call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %{{.*}})
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call i32 @__kmpc_omp_task(
// OGCG:         call void @__kmpc_end_taskgroup(ptr @{{.*}}, i32 %{{.*}})
