// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void work(void);

void single_copyprivate(int *out) {
  int x = 1;
#pragma omp parallel private(x)
  {
#pragma omp single copyprivate(x)
    x = 7;
  }
  *out = x;
}

// CIR-LABEL: cir.func private @__omp_copyprivate_x
// CIR: %[[SRC:.*]] = cir.load {{.*}} %arg1
// CIR: cir.store {{.*}} %[[SRC]], %arg0

// CIR-LABEL: cir.func{{.*}} @single_copyprivate(
// CIR: omp.parallel private(@{{.*}} %[[X:.+]] -> %arg1 : !cir.ptr<!s32i>) {
// CIR: omp.single copyprivate(%arg1 -> @__omp_copyprivate_x
// CIR: cir.call @__captured_stmt
// CIR: omp.terminator
// CIR: }

// LLVM-LABEL: define void @__omp_copyprivate_x
// LLVM: %[[SRC:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM: store i32 %[[SRC]], ptr %{{.+}}, align 4

// LLVM-LABEL: define dso_local void @single_copyprivate(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM: store i32 %[[OUTERXVAL:.+]], ptr %{{.+}}, align 4

// LLVM-LABEL: define {{.*}}void @single_copyprivate..omp_par
// LLVM: %[[DIDIT:[0-9]+]] = alloca i32, align 4
// LLVM: store i32 0, ptr %[[DIDIT]], align 4
// LLVM: call i32 @__kmpc_single(
// LLVM: call void @__kmpc_copyprivate(
// LLVM-SAME: ptr @__omp_copyprivate_x
// LLVM-NOT: call void @__kmpc_barrier(
// LLVM: call void @__captured_stmt(
// LLVM: store i32 1, ptr %[[DIDIT]], align 4
// LLVM: call void @__kmpc_end_single(

// OGCG-LABEL: define {{.*}} @single_copyprivate(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 0, ptr @single_copyprivate.omp_outlined)
// OGCG-LABEL: define internal void @single_copyprivate.omp_outlined(
// OGCG:         store i32 0, ptr %{{.*}}, align 4
// OGCG:         call i32 @__kmpc_single(
// OGCG:         store i32 7, ptr %x, align 4
// OGCG:         call void @__kmpc_end_single(
// OGCG:         call void @__kmpc_copyprivate(
