// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -x c -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -x c -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -x c -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics

typedef void *omp_depend_t;

int x[100];
int y[100];

// LLVM-LABEL: define{{.*}} @task_iterators(
// LLVM: %[[ITER_DEPS:.+]] = alloca {{.*}}, i64 10, align 8
// LLVM: call i32 @__kmpc_omp_task_with_deps(ptr {{.*}}, i32 {{.*}}, ptr {{.*}}, i32 10, ptr %[[ITER_DEPS]], i32 0, ptr null)
void task_iterators(void) {
#pragma omp task depend(iterator(int j=0:5), in: x[j], y[j])
  {
  }
}

// LLVM-LABEL: define{{.*}} @taskwait_iterators(
// LLVM: %[[WAIT_ITER_DEPS:.+]] = alloca {{.*}}, i64 10, align 8
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 10, ptr %[[WAIT_ITER_DEPS]], i32 0, ptr null, i32 0)
void taskwait_iterators(void) {
#pragma omp taskwait depend(iterator(int j=0:5), in: x[j], y[j])
}

// LLVM-LABEL: define{{.*}} @task_depobj(
// LLVM: %[[DEP_COUNT32:.+]] = trunc i64 %{{.+}} to i32
// LLVM: call i32 @__kmpc_omp_task_with_deps(ptr {{.*}}, i32 {{.*}}, ptr {{.*}}, i32 %[[DEP_COUNT32]], ptr %{{.+}}, i32 0, ptr null)
void task_depobj(omp_depend_t d) {
#pragma omp task depend(depobj: d)
  {
  }
}

// LLVM-LABEL: define{{.*}} @taskwait_depobj(
// LLVM: %[[WAIT_DEP_COUNT32:.+]] = trunc i64 %{{.+}} to i32
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 %[[WAIT_DEP_COUNT32]], ptr %{{.+}}, i32 0, ptr null, i32 0)
void taskwait_depobj(omp_depend_t d) {
#pragma omp taskwait depend(depobj: d)
}

// LLVM-LABEL: define{{.*}} @task_all_memory(
// LLVM: store i8 -128, ptr %{{.+}}, align 8
// LLVM: call i32 @__kmpc_omp_task_with_deps(ptr {{.*}}, i32 {{.*}}, ptr {{.*}}, i32 2, ptr %{{.+}}, i32 0, ptr null)
void task_all_memory(int *a, int *b, int *c) {
#pragma omp task depend(in: a) depend(inout: b) depend(out: omp_all_memory)
  {
    *c = 0;
  }
}

// LLVM-LABEL: define{{.*}} @task_shape(
// LLVM: %[[SHAPE_MUL0:.+]] = mul i64 24, %{{.+}}
// LLVM: %[[SHAPE_SIZE:.+]] = mul i64 %[[SHAPE_MUL0]], %{{.+}}
// LLVM: store i64 %[[SHAPE_SIZE]], ptr %{{.+}}, align 8
// LLVM: call i32 @__kmpc_omp_task_with_deps(ptr {{.*}}, i32 {{.*}}, ptr {{.*}}, i32 1, ptr %{{.+}}, i32 0, ptr null)
void task_shape(int a, int *b) {
#pragma omp task depend(in: ([3][a][a])&b)
  {
  }
}

// LLVM-LABEL: define{{.*}} @taskwait_shape(
// LLVM: %[[WAIT_SHAPE_MUL0:.+]] = mul i64 24, %{{.+}}
// LLVM: %[[WAIT_SHAPE_SIZE:.+]] = mul i64 %[[WAIT_SHAPE_MUL0]], %{{.+}}
// LLVM: store i64 %[[WAIT_SHAPE_SIZE]], ptr %{{.+}}, align 8
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 0)
void taskwait_shape(int a, int *b) {
#pragma omp taskwait depend(in: ([3][a][a])&b)
}

// OGCG-LABEL: define dso_local void @task_iterators()
// OGCG: call ptr @__kmpc_omp_task_alloc
// OGCG: call i32 @__kmpc_omp_task_with_deps
// OGCG-LABEL: define dso_local void @taskwait_iterators()
// OGCG: call void @__kmpc_omp_taskwait_deps_51
// OGCG-LABEL: define dso_local void @task_depobj(
// OGCG: call ptr @__kmpc_omp_task_alloc
// OGCG: call i32 @__kmpc_omp_task_with_deps
// OGCG-LABEL: define dso_local void @taskwait_depobj(
// OGCG: call void @__kmpc_omp_taskwait_deps_51
// OGCG-LABEL: define dso_local void @task_all_memory(
// OGCG: call ptr @__kmpc_omp_task_alloc
// OGCG: call i32 @__kmpc_omp_task_with_deps
// OGCG-LABEL: define dso_local void @task_shape(
// OGCG: call ptr @__kmpc_omp_task_alloc
// OGCG: call i32 @__kmpc_omp_task_with_deps
// OGCG-LABEL: define dso_local void @taskwait_shape(
// OGCG: call void @__kmpc_omp_taskwait_deps_51
