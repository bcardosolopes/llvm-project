// RUN: %clang_cc1 -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void before(void);
void after(void);

void emit_simple_barrier() {
  // CIR: cir.func{{.*}}@emit_simple_barrier
  before();
  // CIR-NEXT: cir.call @before
#pragma omp barrier
  // CIR-NEXT: omp.barrier
  after();
  // CIR-NEXT: cir.call @after
}

// LLVM-LABEL: define {{.*}} @emit_simple_barrier()
// LLVM:         call void @before()
// LLVM:         call void @__kmpc_barrier(
// LLVM:         call void @after()

// OGCG-LABEL: define {{.*}} @emit_simple_barrier()
// OGCG:         call void @before()
// OGCG:         call void @__kmpc_barrier(
// OGCG:         call void @after()
