// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s

void foo(void);
void bar(void);

// Implicit single section — no #pragma omp section inside.
void test_sections_implicit(void) {
  #pragma omp sections
  {
    foo();
  }
}
// CHECK-LABEL: define {{.*}} @test_sections_implicit()
// CHECK:         call void @__kmpc_for_static_init_4u(
// CHECK:         switch i32 %{{.*}}, label %{{.*}} [
// CHECK-NEXT:      i32 0, label %[[CASE:.*]]
// CHECK-NEXT:    ]
// CHECK:       [[CASE]]:
// CHECK:         call void @foo()
// CHECK:         call void @__kmpc_for_static_fini(
// CHECK:         call void @__kmpc_barrier(

// Explicit sections.
void test_sections_explicit(void) {
  #pragma omp sections
  {
    #pragma omp section
    foo();
    #pragma omp section
    bar();
  }
}
// CHECK-LABEL: define {{.*}} @test_sections_explicit()
// CHECK:         call void @__kmpc_for_static_init_4u(
// CHECK:         switch i32 %{{.*}}, label %{{.*}} [
// CHECK-NEXT:      i32 0, label %[[CASE0:.*]]
// CHECK-NEXT:      i32 1, label %[[CASE1:.*]]
// CHECK-NEXT:    ]
// CHECK-DAG:     call void @foo()
// CHECK-DAG:     call void @bar()
// CHECK:         call void @__kmpc_for_static_fini(
// CHECK:         call void @__kmpc_barrier(

// Implicit single section under parallel sections.
void test_parallel_sections_implicit(void) {
  #pragma omp parallel sections
  {
    foo();
  }
}
// CHECK-LABEL: define {{.*}} @test_parallel_sections_implicit()
// CHECK:         call void {{.*}} @__kmpc_fork_call(
// CHECK-LABEL: define internal void @test_parallel_sections_implicit..omp_par(
// CHECK-DAG:     call void @__kmpc_for_static_init_4u(
// CHECK-DAG:     call void @foo()
// CHECK-DAG:     call void @__kmpc_for_static_fini(

// Explicit sections under parallel sections.
void test_parallel_sections_explicit(void) {
  #pragma omp parallel sections
  {
    #pragma omp section
    foo();
    #pragma omp section
    bar();
  }
}
// CHECK-LABEL: define {{.*}} @test_parallel_sections_explicit()
// CHECK:         call void {{.*}} @__kmpc_fork_call(
// CHECK-LABEL: define internal void @test_parallel_sections_explicit..omp_par(
// CHECK-DAG:     call void @__kmpc_for_static_init_4u(
// CHECK-DAG:     call void @foo()
// CHECK-DAG:     call void @bar()
// CHECK-DAG:     call void @__kmpc_for_static_fini(

// Parallel sections with reduction.
void test_parallel_sections_reduction(void) {
  int sum = 0;
  #pragma omp parallel sections reduction(+:sum)
  {
    #pragma omp section
    sum += 1;
    #pragma omp section
    sum += 2;
  }
}
// CHECK-LABEL: define {{.*}} @test_parallel_sections_reduction()
// CHECK:         call void {{.*}} @__kmpc_fork_call(
// CHECK-LABEL: define internal void @test_parallel_sections_reduction..omp_par(
// CHECK-DAG:     call void @__kmpc_for_static_init_4u(
// CHECK-DAG:     call void @__kmpc_for_static_fini(
// CHECK-DAG:     call i32 @__kmpc_reduce_nowait(
// CHECK-DAG:     call void @__kmpc_end_reduce_nowait(
// CHECK-DAG:     add nsw i32 %{{.*}}, 1
// CHECK-DAG:     add nsw i32 %{{.*}}, 2
// CHECK-NOT:     call void @__kmpc_barrier(
