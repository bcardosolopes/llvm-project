// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=52 -DOMP52 -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=52 -DOMP52 -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void body(void);
int n;

void ordered_doacross(void) {
  int i;
#pragma omp for ordered(1)
  for (i = 0; i < n; ++i) {
    body();
#ifdef OMP52
#pragma omp ordered doacross(source: omp_cur_iteration)
#else
#pragma omp ordered depend(source)
#endif
    body();
#ifdef OMP52
#pragma omp ordered doacross(sink: omp_cur_iteration - 1)
#else
#pragma omp ordered depend(sink: i - 1)
#endif
    body();
  }
}

// CIR-LABEL: cir.func{{.*}} @ordered_doacross(
// CIR: omp.wsloop ordered(1) {
// CIR: omp.ordered depend_type(dependsource) depend_vec(%{{.*}} : i64)
// CIR: omp.ordered depend_type(dependsink) depend_vec(%{{.*}} : i64)

// LLVM-LABEL: define{{.*}} @ordered_doacross(
// LLVM: call void @llvm.memset
// LLVM-DAG: call void @__kmpc_doacross_init(
// LLVM-DAG: call void @__kmpc_for_static_init_4(
// LLVM-DAG: sext i32 %{{.*}} to i64
// LLVM-DAG: call void @__kmpc_doacross_post(
// LLVM-DAG: sext i32 %{{.*}} to i64
// LLVM-DAG: call void @__kmpc_doacross_wait(
// LLVM-DAG: call void @__kmpc_for_static_fini(
// LLVM-DAG: call void @__kmpc_doacross_fini(
// LLVM-DAG: call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @ordered_doacross()
// OGCG:         call void @__kmpc_doacross_init(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_doacross_post(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_doacross_wait(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_doacross_fini(
// OGCG:         call void @__kmpc_barrier(
