// RUN: %clang_cc1 -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void before(void);
void body(void);
void after(void);

void emit_scope_with_implicit_barrier(void) {
  // CHECK: cir.func{{.*}} @emit_scope_with_implicit_barrier
  before();
  // CHECK-NEXT: cir.call @before
#pragma omp parallel
  {
#pragma omp scope
    body();
  }
  // CHECK: omp.parallel {
  // CHECK: cir.scope {
  // CHECK: cir.call @body
  // CHECK: omp.barrier
  // CHECK: omp.terminator
  after();
  // CHECK: cir.call @after
}

void emit_scope_nowait(void) {
  // CHECK: cir.func{{.*}} @emit_scope_nowait
#pragma omp parallel
  {
#pragma omp scope nowait
    body();
  }
  // CHECK: omp.parallel {
  // CHECK: cir.scope {
  // CHECK: cir.call @body
  // CHECK-NOT: omp.barrier
  // CHECK: omp.terminator
}

// LLVM-LABEL: define {{.*}} @emit_scope_with_implicit_barrier()
// LLVM:         call void @before()
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM:         call void @after()

// LLVM-LABEL: define {{.*}} @emit_scope_nowait()
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// OGCG-LABEL: define {{.*}} @emit_scope_with_implicit_barrier()
// OGCG:         call void @before()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG:         call void @after()
// OGCG-LABEL: define internal void @emit_scope_with_implicit_barrier.omp_outlined(
// OGCG:         call void @body()
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @emit_scope_nowait()
// OGCG:         call void {{.*}} @__kmpc_fork_call(
// OGCG-LABEL: define internal void @emit_scope_nowait.omp_outlined(
// OGCG:         call void @body()
// OGCG-NOT:     call void @__kmpc_barrier(
