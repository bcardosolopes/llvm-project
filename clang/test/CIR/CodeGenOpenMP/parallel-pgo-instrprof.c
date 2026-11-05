// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm \
// RUN:   -fprofile-instrument=clang -disable-llvm-passes %s -o - \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm \
// RUN:   -fprofile-instrument=clang -disable-llvm-passes %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// PGO + OpenMP: the outlined parallel-region function must contain a real
// `@llvm.instrprof.increment` intrinsic. CIR codegen first emits a private
// stub `@__cir_instrprof_increment` inside the parallel region; the
// MLIR-to-LLVM translation then rewrites that stub to the real intrinsic
// using the per-region PGO func-name var carried as a discardable
// `cir.instrprof_func_name` attribute on the `omp.parallel` op.
//
// This test pins the post-rewrite shape so that future refactors of the
// CIR-vs-OpenMP-translator split preserve
// the rewrite.

void work(void);

void f(void) {
#pragma omp parallel
  { work(); }
}

// CIR: define dso_local void @f()
// CIR:   call void @llvm.instrprof.increment(ptr {{.*}}@__profn_f
// CIR:   call void {{.*}}@__kmpc_fork_call(ptr {{.*}}, i32 0, ptr @f..omp_par)
//
// CIR-LABEL: define internal void @f..omp_par(
// CIR-NOT:   call void @__cir_instrprof_increment
// CIR:       call void @llvm.instrprof.increment(ptr {{.*}}@__profn__unknown__f.omp_outlined

// OGCG: define dso_local void @f()
// OGCG:   call void @llvm.instrprof.increment(
// OGCG:   call void {{.*}}@__kmpc_fork_call(
//
// OGCG-LABEL: define internal void @f.omp_outlined(
// OGCG:       call void @llvm.instrprof.increment(
