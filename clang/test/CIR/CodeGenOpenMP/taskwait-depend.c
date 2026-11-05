// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int taskwait_depend_in(int argc) {
  // CIR-LABEL: cir.func{{.*}}@taskwait_depend_in
#pragma omp taskwait depend(in : argc)
  // CIR: omp.taskwait depend(taskdependin -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))
  return argc;
}

void taskwait_depend_out(int *ptr) {
  int x = 0;
  // CIR-LABEL: cir.func{{.*}}@taskwait_depend_out
#pragma omp taskwait depend(out : x)
  // CIR: omp.taskwait depend(taskdependout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))
  *ptr = x;
}

void taskwait_depend_multiple(int x, int y) {
  // CIR-LABEL: cir.func{{.*}}@taskwait_depend_multiple
#pragma omp taskwait depend(in : x) depend(out : y)
  // CIR: omp.taskwait depend(taskdependin -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64), taskdependout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))
  return;
}

void taskwait_depend_nowait(int argc) {
  // CIR-LABEL: cir.func{{.*}}@taskwait_depend_nowait
#pragma omp taskwait depend(in : argc) nowait
  // CIR: omp.taskwait depend(taskdependin -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64)) nowait
  return;
}

void taskwait_depend_dynamic(int *p, int i, int n) {
  // CIR-LABEL: cir.func{{.*}}@taskwait_depend_dynamic
#pragma omp taskwait depend(inout : p[i:n])
  // CIR: %[[SIZE:.*]] = llvm.mul %{{.*}}, %{{.*}} : i64
  // CIR: omp.taskwait depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%[[SIZE]] : i64))
  return;
}

// LLVM-LABEL: define dso_local i32 @taskwait_depend_in(
// LLVM: %[[DEPARR_IN:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// LLVM: %[[DEP_IN:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[DEPARR_IN]], i64 0, i64 0
// LLVM: %[[FLAGS_IN:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[DEP_IN]], i32 0, i32 2
// LLVM: store i8 1, ptr %[[FLAGS_IN]], align 1
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %[[DEPARR_IN]], i32 0, ptr null, i32 0)

// LLVM-LABEL: define dso_local void @taskwait_depend_out(
// LLVM: %[[DEPARR_OUT:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// LLVM: %[[DEP_OUT:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[DEPARR_OUT]], i64 0, i64 0
// LLVM: %[[FLAGS_OUT:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[DEP_OUT]], i32 0, i32 2
// LLVM: store i8 3, ptr %[[FLAGS_OUT]], align 1
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %[[DEPARR_OUT]], i32 0, ptr null, i32 0)

// LLVM-LABEL: define dso_local void @taskwait_depend_multiple(
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 2, ptr %{{.+}}, i32 0, ptr null, i32 0)

// LLVM-LABEL: define dso_local void @taskwait_depend_nowait(
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 1)

// LLVM-LABEL: define dso_local void @taskwait_depend_dynamic(
// LLVM: %[[SIZE:.+]] = mul i64 %{{.+}}, 4
// LLVM: store i64 %[[SIZE]], ptr %{{.+}}, align 8
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 0)

// OGCG-LABEL: define dso_local i32 @taskwait_depend_in(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 0)
// OGCG-LABEL: define dso_local void @taskwait_depend_out(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 0)
// OGCG-LABEL: define dso_local void @taskwait_depend_multiple(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 2, ptr %{{.+}}, i32 0, ptr null, i32 0)
// OGCG-LABEL: define dso_local void @taskwait_depend_nowait(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 1)
// OGCG-LABEL: define dso_local void @taskwait_depend_dynamic(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %{{.+}}, i32 0, ptr null, i32 0)
