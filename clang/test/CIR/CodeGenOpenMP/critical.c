// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void foo(void);

void test_critical(void) {
  // Unnamed critical region.
  #pragma omp critical
  foo();
  // Named critical region.
  #pragma omp critical(the_name)
  foo();
  // Named critical region with contradictory hints (implementation-defined per
  // OpenMP spec, not an error — the runtime receives the raw hint value).
  #pragma omp critical(the_name1) hint(23)
  foo();
}

// CHECK-LABEL: define {{.*}} @test_critical()
// CHECK: call void @__kmpc_critical(
// CHECK: call void @foo()
// CHECK: call void @__kmpc_end_critical(
// CHECK: call void @__kmpc_critical_with_hint({{.*}}, i32 0)
// CHECK: call void @foo()
// CHECK: call void @__kmpc_end_critical(
// CHECK: call void @__kmpc_critical_with_hint({{.*}}, i32 23)
// CHECK: call void @foo()
// CHECK: call void @__kmpc_end_critical(

// OGCG-LABEL: define {{.*}} @test_critical()
// OGCG: call void @__kmpc_critical(
// OGCG: call void @foo()
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @foo()
// OGCG: call void @__kmpc_end_critical(
// OGCG: call void @__kmpc_critical_with_hint({{.*}}, i32 23)
// OGCG: call void @foo()
// OGCG: call void @__kmpc_end_critical(
