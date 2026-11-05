// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp %s -emit-llvm -o - -disable-llvm-optzns | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir %s -emit-llvm -o - -disable-llvm-optzns | FileCheck %s --check-prefix=CIR %}

void work12(int, int);

void foo(int q) {
  int p = 2;

#pragma omp target teams firstprivate(p)
  work12(p, p);
}

void bar(int q, int n, int tl) {
  int p = 2;

#pragma omp target teams firstprivate(p) num_teams(n) thread_limit(tl)
  work12(p, p);
}

// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.+}}, i32 1, ptr @{{.+}}, i32 %{{.+}})
// OGCG-LABEL: define dso_local void @bar(
// OGCG: call void @__omp_offloading{{.*}}bar{{.*}}(i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}})
// OGCG: call void @__kmpc_push_num_teams(ptr @{{.+}}, i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}})
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.+}}, i32 1, ptr @__omp_offloading{{.*}}bar{{.*}}.omp_outlined, i32 %{{.+}})

// CIR: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.+}}, i32 1, ptr @{{.+}}, i32 %{{.+}})
// CIR-LABEL: define dso_local void @bar(
// CIR: %[[N_INIT:[0-9]+]] = load i32, ptr %{{.+}}, align 4
// CIR: store i32 %[[N_INIT]], ptr %{{.+}}, align 4
// CIR: %[[TL_INIT:[0-9]+]] = load i32, ptr %{{.+}}, align 4
// CIR: store i32 %[[TL_INIT]], ptr %{{.+}}, align 4
// CIR: call void @__kmpc_push_num_teams_51(ptr @{{.+}}, i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}})
// CIR: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.+}}, i32 1, ptr @bar{{.+}}, i32 %{{.+}})
// CIR: define internal void @{{.+}}(ptr {{.+}}, ptr {{.+}}, i32 {{.+}})
