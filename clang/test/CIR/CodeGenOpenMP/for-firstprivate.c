// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void use(int);

// Firstprivate with local variable.
void test_for_firstprivate_local(int n) {
  int x = 5;
#pragma omp parallel
#pragma omp for firstprivate(x)
  for (int i = 0; i < n; i++)
    use(x + i);
}

// CIR-LABEL: cir.func{{.*}} @test_for_firstprivate_local(
// CIR: omp.parallel {
// CIR: omp.wsloop{{.*}} private(@__omp_firstprivate_x_{{.*}} %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest

// LLVM-LABEL: define dso_local void @test_for_firstprivate_local(
// LLVM-LABEL: define internal void @test_for_firstprivate_local..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(

// Firstprivate with global variable.
int g_val = 42;

void test_for_firstprivate_global(int n) {
#pragma omp parallel
#pragma omp for firstprivate(g_val)
  for (int i = 0; i < n; i++)
    use(g_val + i);
}

// CIR-LABEL: cir.func{{.*}} @test_for_firstprivate_global(
// CIR: omp.parallel {
// CIR: cir.get_global @g_val
// CIR: omp.wsloop{{.*}} private(@__omp_firstprivate_g_val_{{.*}} %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
// CIR: omp.loop_nest

// LLVM-LABEL: define dso_local void @test_for_firstprivate_global(
// LLVM-LABEL: define internal void @test_for_firstprivate_global..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(

// OGCG-LABEL: define dso_local void @test_for_firstprivate_local(
// OGCG-LABEL: define internal void @test_for_firstprivate_local.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
// OGCG: call void @__kmpc_barrier(

// OGCG-LABEL: define dso_local void @test_for_firstprivate_global(
// OGCG-LABEL: define internal void @test_for_firstprivate_global.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
// OGCG: call void @__kmpc_barrier(
