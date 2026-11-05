// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
//
// Pins the carve-out boundary contract for the
// `num_threads_modifier::strict` TD operand on `omp.parallel`. CIRGen
// continues to set the TD operand (so the dialect verifier — which
// requires severity/message clauses to imply num_threads(strict) —
// stays consistent) AND a CIR-namespaced discardable attribute
// `cir.omp.num_threads_modifier`; the OpenMP-to-LLVM translator reads
// the discardable attribute via a thin shim. The lowered runtime
// dispatch must remain `__kmpc_push_num_threads_strict` (vs. the
// non-strict `__kmpc_push_num_threads` for plain `num_threads(...)`).
//
// This test exercises the strict-vs-non-strict dispatch side by side
// with the OG codegen and pins the discardable attribute on the
// emitted `omp.parallel` op so any future TD-operand removal cannot
// silently regress the runtime call.

void strict_path(int n) {
#pragma omp parallel num_threads(strict : n)
  { }
}

void plain_path(int n) {
#pragma omp parallel num_threads(n)
  { }
}

// CIR-LABEL: cir.func{{.*}} @strict_path(
// CIR: omp.parallel num_threads(strict: %{{.*}} : i32)

// CIR-LABEL: cir.func{{.*}} @plain_path(
// CIR: omp.parallel num_threads(%{{.*}} : i32)

// LLVM-LABEL: define{{.*}} @strict_path(
// LLVM: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{[12]}}, ptr {{.*}})

// LLVM-LABEL: define{{.*}} @plain_path(
// LLVM: call void @__kmpc_push_num_threads(ptr {{.*}}, i32 {{.*}}, i32 {{.*}})

// OGCG-LABEL: define{{.*}} @strict_path(
// OGCG: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{[12]}}, ptr {{.*}})

// OGCG-LABEL: define{{.*}} @plain_path(
// OGCG: call void @__kmpc_push_num_threads(ptr {{.*}}, i32 {{.*}}, i32 {{.*}})
