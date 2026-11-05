// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void task_body(void);
void loop_body(int);

void task_i386(void) {
#pragma omp task
  task_body();
}

// LLVM-LABEL: define dso_local void @task_i386(
// LLVM: %[[GTID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call ptr @__kmpc_omp_task_alloc(ptr @{{.*}}, i32 %[[GTID]], i32 1, i32 20, i32 0, ptr @task_i386..omp_par)
// LLVM: call i32 @__kmpc_omp_task(ptr @{{.*}}, i32 %[[GTID]], ptr %{{.*}})

void taskloop_i386(int n) {
#pragma omp taskloop
  for (int i = 0; i < n; ++i)
    loop_body(i);
}

// LLVM-LABEL: define dso_local void @taskloop_i386(
// LLVM: %[[CTX:.*]] = {{(tail )?}}call ptr @malloc(i32 4)
// LLVM: %[[GTID2:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call ptr @__kmpc_omp_task_alloc(ptr @{{.*}}, i32 %[[GTID2]], i32 1, i32 {{[0-9]+}}, i32 {{[0-9]+}}, ptr @taskloop_i386..omp_par)
// LLVM: call void @llvm.memcpy.p0.p0.i32(

// LLVM-LABEL: define internal void @omp_taskloop_dup(
// LLVM: {{(tail )?}}call ptr @malloc(i32 4)

// OGCG-LABEL: define dso_local void @task_i386(
// OGCG: %[[GTID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// OGCG: call ptr @__kmpc_omp_task_alloc(ptr @{{.*}}, i32 %[[GTID]], i32 1, i32 20, i32 0, ptr @.omp_task_entry.)
// OGCG: call i32 @__kmpc_omp_task(ptr @{{.*}}, i32 %[[GTID]], ptr %{{.*}})

// OGCG-LABEL: define dso_local void @taskloop_i386(
// OGCG: %[[GTID2:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// OGCG: call ptr @__kmpc_omp_task_alloc(ptr @{{.*}}, i32 %[[GTID2]], i32 1, i32 {{[0-9]+}}, i32 {{[0-9]+}}, ptr @.omp_task_entry.{{.*}})
// OGCG: call void @__kmpc_taskloop(
