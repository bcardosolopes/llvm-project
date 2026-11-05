// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -x c -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -x c -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -x c -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics

typedef void *omp_depend_t;

int x[100];
int y[100];

// LLVM-LABEL: define{{.*}} @target_iterators(
// LLVM: %[[ITER_DEPS:.+]] = alloca {{.*}}, i64 5, align 8
// LLVM: call ptr @__kmpc_omp_task_alloc(
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 5, ptr %[[ITER_DEPS]], i32 0, ptr null, i32 0)
// LLVM: call void @__kmpc_omp_task_begin_if0(
// LLVM: call void @__kmpc_omp_task_complete_if0(
void target_iterators(void) {
#pragma omp target depend(iterator(int j=0:5), in: x[j])
  {
    x[0]++;
  }
}

// LLVM-LABEL: define{{.*}} @target_nowait_iterators(
// LLVM: %[[NOWAIT_ITER_DEPS:.+]] = alloca {{.*}}, i64 5, align 8
// LLVM: call ptr @__kmpc_omp_target_task_alloc(
// LLVM: call i32 @__kmpc_omp_task_with_deps(ptr {{.*}}, i32 {{.*}}, ptr {{.*}}, i32 5, ptr %[[NOWAIT_ITER_DEPS]], i32 0, ptr null)
void target_nowait_iterators(void) {
#pragma omp target nowait depend(iterator(int j=0:5), in: x[j])
  {
    x[0]++;
  }
}

// LLVM-LABEL: define{{.*}} @target_depobj(
// LLVM: %[[DEP_COUNT32:.+]] = trunc i64 %{{.+}} to i32
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 %[[DEP_COUNT32]], ptr %{{.+}}, i32 0, ptr null, i32 0)
void target_depobj(omp_depend_t d) {
#pragma omp target depend(depobj: d)
  {
    x[0]++;
  }
}

// LLVM-LABEL: define{{.*}} @target_all_memory(
// LLVM: store i8 -128, ptr %{{.+}}, align 8
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 2, ptr %{{.+}}, i32 0, ptr null, i32 0)
void target_all_memory(int *a, int *b, int *c) {
#pragma omp target depend(in: a) depend(inout: b) depend(out: omp_all_memory)
  {
    *c = 0;
  }
}

// LLVM-LABEL: define{{.*}} @target_shape(
// LLVM: %[[SHAPE_MUL0:.+]] = mul i64 24, %{{.+}}
// LLVM: %[[SHAPE_SIZE:.+]] = mul i64 %[[SHAPE_MUL0]], %{{.+}}
// LLVM: store i64 %[[SHAPE_SIZE]], ptr %{{.+}}, align 8
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 0)
void target_shape(int a, int *b) {
#pragma omp target depend(in: ([3][a][a])&b)
  {
  }
}

// OGCG-LABEL: define{{.*}} @target_iterators(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define{{.*}} @target_nowait_iterators(
// OGCG:         call ptr @__kmpc_omp_target_task_alloc(
// OGCG:         call i32 @__kmpc_omp_task_with_deps(

// OGCG-LABEL: define{{.*}} @target_depobj(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define{{.*}} @target_all_memory(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define{{.*}} @target_shape(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(
