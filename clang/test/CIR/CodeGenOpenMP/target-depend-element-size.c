// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sink(int *);
void work(void);

void target_parallel_depend(int *p) {
#pragma omp target parallel depend(inout : p[0])
  sink(p);
}

void target_teams_depend(int *p) {
#pragma omp target teams depend(inout : p[0])
  work();
}

void target_parallel_depend_section(int *p) {
#pragma omp target parallel depend(inout : p[0:2])
  sink(p);
}

void target_parallel_depend_dynamic(int *p, int i, int n) {
#pragma omp target parallel depend(inout : p[i:n])
  sink(p);
}

void target_teams_depend_section(int *p) {
#pragma omp target teams depend(inout : p[0:2])
  work();
}

void target_parallel_depend_row(int a[10][20]) {
#pragma omp target parallel depend(inout : a[1])
  sink(&a[0][0]);
}

void target_parallel_depend_nested_section_subscript(int a[10][20]) {
#pragma omp target parallel depend(inout : a[1:2][0])
  sink(&a[0][0]);
}

void target_parallel_depend_nested_subscript_section(int a[10][20]) {
#pragma omp target parallel depend(inout : a[1][2:4])
  sink(&a[0][0]);
}

// CHECK-LABEL: define dso_local void @target_parallel_depend(
// CHECK: %[[TARGET_PAR_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_PAR_SIZE]], ptr %{{.+}}, align 8

// CHECK-LABEL: define dso_local void @target_teams_depend(
// CHECK: %[[TARGET_TEAMS_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_TEAMS_SIZE]], ptr %{{.+}}, align 8

// CIR-LABEL: cir.func no_inline optnone dso_local @target_parallel_depend_section(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))

// CIR-LABEL: cir.func no_inline optnone dso_local @target_parallel_depend_dynamic(
// CIR: %[[SIZE:.*]] = llvm.mul %{{.*}}, %{{.*}} : i64
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%[[SIZE]] : i64))

// CIR-LABEL: cir.func no_inline optnone dso_local @target_teams_depend_section(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))

// CHECK-LABEL: define dso_local void @target_parallel_depend_section(
// CHECK: %[[TARGET_PAR_SECTION_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_PAR_SECTION_SIZE]], ptr %{{.+}}, align 8

// CHECK-LABEL: define dso_local void @target_parallel_depend_dynamic(
// CHECK: %[[SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[SIZE]], ptr %{{.+}}, align 8

// CHECK-LABEL: define dso_local void @target_teams_depend_section(
// CHECK: %[[TARGET_TEAMS_SECTION_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_TEAMS_SECTION_SIZE]], ptr %{{.+}}, align 8

// CIR-LABEL: cir.func no_inline optnone dso_local @target_parallel_depend_row(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))

// CHECK-LABEL: define dso_local void @target_parallel_depend_row(
// CHECK: %[[TARGET_ROW_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_ROW_SIZE]], ptr %{{.+}}, align 8

// CIR-LABEL: cir.func no_inline optnone dso_local @target_parallel_depend_nested_section_subscript(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))

// CHECK-LABEL: define dso_local void @target_parallel_depend_nested_section_subscript(
// CHECK: %[[TARGET_NESTED_SECTION_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_NESTED_SECTION_SIZE]], ptr %{{.+}}, align 8

// CIR-LABEL: cir.func no_inline optnone dso_local @target_parallel_depend_nested_subscript_section(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))

// CHECK-LABEL: define dso_local void @target_parallel_depend_nested_subscript_section(
// CHECK: %[[TARGET_NESTED_SUBSCRIPT_SIZE:.+]] = mul i64 %{{.+}}, 4
// CHECK: store i64 %[[TARGET_NESTED_SUBSCRIPT_SIZE]], ptr %{{.+}}, align 8

// OGCG-LABEL: define dso_local void @target_parallel_depend(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_teams_depend(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_parallel_depend_section(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_parallel_depend_dynamic(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_teams_depend_section(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_parallel_depend_row(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_parallel_depend_nested_section_subscript(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define dso_local void @target_parallel_depend_nested_subscript_section(
// OGCG:         call ptr @__kmpc_omp_task_alloc(
// OGCG:         call void @__kmpc_omp_taskwait_deps_51(
