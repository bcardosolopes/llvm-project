// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=60 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
//
// Pins the boundary contract that any future carve-out of the
// `severity_kind` and `message_var` TD operands on `omp.parallel` must
// preserve. Both clauses are extensions on
// upstream-shared `omp.parallel`; the recommended carve-out shape is
// CIR producing a discardable `cir.omp.severity_kind` /
// `cir.omp.message_var_sym` attribute and the OpenMP-to-LLVM translator
// reading via a thin shim. The shim must keep the same final
// `__kmpc_push_num_threads_strict` argument layout as today:
//
//   __kmpc_push_num_threads_strict(ident, gtid, num_threads, severity, msg)
//
// where severity is `i32 1` for `warning` and `i32 2` for `fatal`, and
// msg is `ptr null` if `message(...)` is omitted. This test exercises
// both severity values + the message-omitted path side by side.

void warn_no_msg(int n) {
#pragma omp parallel num_threads(strict : n) severity(warning)
  { }
}

void fatal_no_msg(int n) {
#pragma omp parallel num_threads(strict : n) severity(fatal)
  { }
}

void warn_with_msg(int n) {
#pragma omp parallel num_threads(strict : n) severity(warning) message("over-subscribed")
  { }
}

// CIR-LABEL: cir.func{{.*}} @warn_no_msg(
// CIR: omp.parallel num_threads(strict: %{{.*}} : i32) severity(warning)

// CIR-LABEL: cir.func{{.*}} @fatal_no_msg(
// CIR: omp.parallel num_threads(strict: %{{.*}} : i32) severity(fatal)

// CIR-LABEL: cir.func{{.*}} @warn_with_msg(
// CIR: omp.parallel num_threads(strict: %{{.*}} : i32) severity(warning) message(%{{.*}} : !cir.ptr<!s8i>)

// LLVM-LABEL: define{{.*}} @warn_no_msg(
// LLVM: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 1, ptr null)

// LLVM-LABEL: define{{.*}} @fatal_no_msg(
// LLVM: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 2, ptr null)

// LLVM-LABEL: define{{.*}} @warn_with_msg(
// LLVM: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 1, ptr {{.*}})

// OGCG-LABEL: define{{.*}} @warn_no_msg(
// OGCG: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 1, ptr null)

// OGCG-LABEL: define{{.*}} @fatal_no_msg(
// OGCG: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 2, ptr null)

// OGCG-LABEL: define{{.*}} @warn_with_msg(
// OGCG: call void @__kmpc_push_num_threads_strict(ptr {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 1, ptr {{.*}})
