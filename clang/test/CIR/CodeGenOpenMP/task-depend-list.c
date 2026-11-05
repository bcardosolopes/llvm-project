// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics
//
// Pins the carve-out boundary contract for the
// `taskdependlist` enum case on the upstream-shared
// `mlir::omp::ClauseTaskDepend` enum. CIRGen continues to set the
// shared enum case (so the dialect verifier — which special-cases
// the prebuilt depend-payload form via the enum case — stays
// consistent) AND a CIR-namespaced discardable marker
// `cir.omp.taskdependlist` (UnitAttr); the OpenMP-to-LLVM
// translator gates its prebuilt-payload fast path on the marker via
// a thin shim. The lowered runtime dispatch must remain
// `__kmpc_omp_taskwait_deps_51` with the prebuilt `depArray` (vs.
// the per-entry depend-record marshalling for plain `depend(in: ...)`
// dependencies).
//
// This test exercises the prebuilt-payload path (depend-iterator) so
// any future enum-case removal cannot silently regress the runtime
// dispatch shape.

int x[100];

void target_iterators(void) {
#pragma omp target depend(iterator(int j=0:5), in: x[j])
  {
    x[0]++;
  }
}

// CIR-LABEL: cir.func{{.*}} @target_iterators(
// CIR: omp.target depend(taskdependlist
// CIR: } {{{.*}}cir.omp.taskdependlist{{.*}}}

// LLVM-LABEL: define{{.*}} @target_iterators(
// LLVM: %[[ITER_DEPS:.+]] = alloca {{.*}}, i64 5, align 8
// LLVM: call void @__kmpc_omp_taskwait_deps_51(ptr {{.*}}, i32 {{.*}}, i32 5, ptr %[[ITER_DEPS]], i32 0, ptr null, i32 0)

// OGCG-LABEL: define{{.*}} @target_iterators(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(
