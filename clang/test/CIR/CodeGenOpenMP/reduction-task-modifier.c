// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);

void parallel_reduction_task(int x) {
  // CIR-LABEL: cir.func{{.*}} @parallel_reduction_task(
#pragma omp parallel reduction(task, +: x)
  {
    // CIR: omp.parallel reduction(mod: task, @__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
    body();
  }
}

// LLVM-LABEL: define internal void @parallel_reduction_task..omp_par(
// LLVM: call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 0, i32 1, ptr %{{.*}})
// LLVM: call void @body()
// LLVM: call void @__kmpc_task_reduction_modifier_fini(ptr @{{.*}}, i32 %{{.*}}, i32 0)
// LLVM: call i32 @__kmpc_reduce(

void for_reduction_task(int x) {
  // CIR-LABEL: cir.func{{.*}} @for_reduction_task(
#pragma omp parallel shared(x)
  {
#pragma omp for reduction(task, +: x)
    for (int i = 0; i < 10; ++i) {
      // CIR: omp.parallel {
      // CIR: omp.wsloop reduction(mod: task, @__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
      body();
    }
  }
}

// LLVM-LABEL: define internal void @for_reduction_task..omp_par(
// LLVM: call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 1, i32 1, ptr %{{.*}})
// LLVM: call void @__kmpc_task_reduction_modifier_fini(ptr @{{.*}}, i32 %{{.*}}, i32 1)
// LLVM: call i32 @__kmpc_reduce(
// LLVM: call void @body()

void parallel_for_reduction_task(int x) {
  // CIR-LABEL: cir.func{{.*}} @parallel_for_reduction_task(
#pragma omp parallel for reduction(task, +: x)
  for (int i = 0; i < 10; ++i) {
    // CIR: omp.parallel
    // CIR: omp.wsloop nowait reduction(mod: task, @__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
    body();
  }
}

// LLVM-LABEL: define internal void @parallel_for_reduction_task..omp_par(
// LLVM: call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 1, i32 1, ptr %{{.*}})
// LLVM: call void @__kmpc_task_reduction_modifier_fini(ptr @{{.*}}, i32 %{{.*}}, i32 1)
// LLVM: call i32 @__kmpc_reduce_nowait(
// LLVM: call void @body()

void sections_reduction_task(int x) {
  // CIR-LABEL: cir.func{{.*}} @sections_reduction_task(
#pragma omp parallel
  {
#pragma omp sections reduction(task, +: x)
    {
      // CIR: omp.parallel {
      // CIR: omp.sections reduction(mod: task, @__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
#pragma omp section
      body();
    }
  }
}

// LLVM-LABEL: define internal void @sections_reduction_task..omp_par(
// LLVM: call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 1, i32 1, ptr %{{.*}})
// LLVM: call void @__kmpc_task_reduction_modifier_fini(ptr @{{.*}}, i32 %{{.*}}, i32 1)
// LLVM: call i32 @__kmpc_reduce(
// LLVM: call void @body()

// OGCG-LABEL: define dso_local void @parallel_reduction_task(
// OGCG: call void {{.*}}@__kmpc_fork_call
// OGCG-DAG: define internal void @parallel_reduction_task.omp_outlined(
// OGCG-DAG: call ptr @__kmpc_taskred_modifier_init
// OGCG-DAG: call void @body()
// OGCG-DAG: call void @__kmpc_task_reduction_modifier_fini
// OGCG-DAG: call i32 @__kmpc_reduce
// OGCG-LABEL: define dso_local void @for_reduction_task(
// OGCG: call void {{.*}}@__kmpc_fork_call
// OGCG-DAG: define internal void @for_reduction_task.omp_outlined(
// OGCG-DAG: call ptr @__kmpc_taskred_modifier_init
// OGCG-DAG: call void @__kmpc_task_reduction_modifier_fini
// OGCG-DAG: call i32 @__kmpc_reduce
// OGCG-DAG: call void @body()
// OGCG-LABEL: define dso_local void @parallel_for_reduction_task(
// OGCG: call void {{.*}}@__kmpc_fork_call
// OGCG-DAG: define internal void @parallel_for_reduction_task.omp_outlined(
// OGCG-DAG: call ptr @__kmpc_taskred_modifier_init
// OGCG-DAG: call void @__kmpc_task_reduction_modifier_fini
// OGCG-DAG: call i32 @__kmpc_reduce_nowait
// OGCG-DAG: call void @body()
// OGCG-LABEL: define dso_local void @sections_reduction_task(
// OGCG: call void {{.*}}@__kmpc_fork_call
// OGCG-DAG: define internal void @sections_reduction_task.omp_outlined(
// OGCG-DAG: call ptr @__kmpc_taskred_modifier_init
// OGCG-DAG: call void @__kmpc_task_reduction_modifier_fini
// OGCG-DAG: call i32 @__kmpc_reduce
// OGCG-DAG: call void @body()
