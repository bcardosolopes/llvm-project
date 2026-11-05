// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void parallel_private_only(int *out) {
  int x = 1;
#pragma omp parallel private(x)
  x = 7;
  *out = x;
}

// CIR-LABEL: cir.func{{.*}} @parallel_private_only(
// CIR: %[[X:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init]
// CIR: %[[C1:.+]] = cir.const #cir.int<1> : !s32i
// CIR: cir.store{{.*}} %[[C1]], %[[X]]
// CIR: omp.parallel private(@{{.*}} %[[X]] -> %arg1 : !cir.ptr<!s32i>) {
// CIR: cir.scope {
// CIR: %[[C7:.+]] = cir.const #cir.int<7> : !s32i
// CIR: cir.store{{.*}} %[[C7]], %arg1
// CIR: }
// CIR: omp.terminator
// CIR: }
// CIR: %[[OUTERX:.+]] = cir.load {{.*}} %[[X]]
// CIR: cir.store{{.*}} %[[OUTERX]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: define dso_local void @parallel_private_only(
// LLVM: store i32 1, ptr %[[X:.+]], align 4
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM: %[[OUTERXVAL:.+]] = load i32, ptr %[[X]], align 4
// LLVM: store i32 %[[OUTERXVAL]], ptr %{{.+}}, align 4

// LLVM-LABEL: define {{.*}}void @parallel_private_only..omp_par
// LLVM: %omp.private.alloc = alloca i32, align 4
// LLVM: store i32 7, ptr %omp.private.alloc, align 4

// OGCG-LABEL: define {{.*}} @parallel_private_only(
// OGCG:         store i32 1, ptr %{{.*}}, align 4
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 0, ptr @parallel_private_only.omp_outlined)
// OGCG:         store i32 %{{.*}}, ptr %{{.*}}, align 4
// OGCG-LABEL: define internal void @parallel_private_only.omp_outlined(
// OGCG:         %x = alloca i32, align 4
// OGCG:         store i32 7, ptr %x, align 4
