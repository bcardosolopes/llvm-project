// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void use(int *);

// Standalone sections with private clause.
// CIR-LABEL: cir.func{{.*}} @test_sections_private(
// CIR:       omp.sections private(@{{.*}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:         omp.section {
// CIR:         ^bb0(%[[PRIVATE0:.+]]: !cir.ptr<!s32i>
// CIR:           cir.call @use(%[[PRIVATE0]]) : (!cir.ptr<!s32i>) -> ()
// CIR:         omp.section {
// CIR:         ^bb0(%[[PRIVATE1:.+]]: !cir.ptr<!s32i>
// CIR:           cir.call @use(%[[PRIVATE1]]) : (!cir.ptr<!s32i>) -> ()
// LLVM-LABEL: define internal void @test_sections_private..omp_par(
// LLVM:       %omp.private.alloc = alloca i32, align 4
// LLVM:       call void @use(ptr noundef %omp.private.alloc)
// LLVM:       call void @use(ptr noundef %omp.private.alloc)
void test_sections_private(int x) {
#pragma omp parallel
  {
#pragma omp sections private(x)
    {
#pragma omp section
      use(&x);
#pragma omp section
      use(&x);
    }
  }
}

// Standalone sections with firstprivate clause.
// CIR-LABEL: cir.func{{.*}} @test_sections_firstprivate(
// CIR:       omp.sections private(@{{.*}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:         omp.section {
// CIR:         ^bb0(%[[FIRSTPRIVATE:.+]]: !cir.ptr<!s32i>
// CIR:           cir.call @use(%[[FIRSTPRIVATE]]) : (!cir.ptr<!s32i>) -> ()
// LLVM-LABEL: define internal void @test_sections_firstprivate..omp_par(
// LLVM:       %omp.private.alloc = alloca i32, align 4
// LLVM:       store i32 %{{.+}}, ptr %omp.private.alloc, align 4
// LLVM:       call void @use(ptr noundef %omp.private.alloc)
void test_sections_firstprivate(int x) {
#pragma omp parallel
  {
#pragma omp sections firstprivate(x)
    {
#pragma omp section
      use(&x);
    }
  }
}

// Combined parallel sections with a sections-side private clause.
// CIR-LABEL: cir.func{{.*}} @test_parallel_sections_private(
// CIR:       omp.parallel
// CIR:         cir.scope {
// CIR:           omp.sections nowait private(@{{.*}} %{{.+}} -> %{{.+}} : !cir.ptr<!s32i>) {
// CIR:             omp.section {
// CIR:             ^bb0(%[[PARPRIVATE0:.+]]: !cir.ptr<!s32i>
// CIR:               cir.call @use(%[[PARPRIVATE0]]) : (!cir.ptr<!s32i>) -> ()
// CIR:             omp.section {
// CIR:             ^bb0(%[[PARPRIVATE1:.+]]: !cir.ptr<!s32i>
// CIR:               cir.call @use(%[[PARPRIVATE1]]) : (!cir.ptr<!s32i>) -> ()
void test_parallel_sections_private(int x) {
#pragma omp parallel sections private(x)
  {
#pragma omp section
    use(&x);
#pragma omp section
    use(&x);
  }
}

// OGCG-LABEL: define {{.*}} @test_sections_private(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 0, ptr @test_sections_private.omp_outlined)
// OGCG-LABEL: define internal void @test_sections_private.omp_outlined(
// OGCG:         %x = alloca i32, align 4
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @use(ptr noundef %x)
// OGCG:         call void @use(ptr noundef %x)
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @test_sections_firstprivate(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 1, ptr @test_sections_firstprivate.omp_outlined,
// OGCG-LABEL: define internal void @test_sections_firstprivate.omp_outlined(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @use(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @test_parallel_sections_private(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 0, ptr @test_parallel_sections_private.omp_outlined)
// OGCG-LABEL: define internal void @test_parallel_sections_private.omp_outlined(
// OGCG:         %x = alloca i32, align 4
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @use(ptr noundef %x)
// OGCG:         call void @use(ptr noundef %x)
// OGCG:         call void @__kmpc_for_static_fini(
