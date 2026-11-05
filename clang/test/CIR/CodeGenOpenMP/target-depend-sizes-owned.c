// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// expected-no-diagnostics
//
// Pins the carve-out boundary contract for the `depend_sizes`
// variadic SSA operand on the upstream-shared depend-clause-bearing
// ops. CIRGen continues to populate
// the shared TD operand (so the dialect verifier and printer stay
// consistent) AND a CIR-namespaced discardable marker
// `cir.omp.depend_sizes_owned` (UnitAttr); the OpenMP-to-LLVM
// translator's `buildDependData` callers route the operand through
// a thin shim `getCIRDependSizes` that returns the operand range only
// when the marker is present. The lowered runtime depend-record
// `Len` field must remain the per-entry byte length stored into the
// dependence struct so any future operand removal cannot silently
// regress the runtime ABI shape.

void sink(int *);

void target_parallel_depend_section(int *p) {
#pragma omp target parallel depend(inout : p[0:2])
  sink(p);
}

void target_parallel_depend_dynamic(int *p, int i, int n) {
#pragma omp target parallel depend(inout : p[i:n])
  sink(p);
}

// CIR-LABEL: cir.func{{.*}} @target_parallel_depend_section(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))
// CIR: } {{.*}}cir.omp.depend_sizes_owned

// CIR-LABEL: cir.func{{.*}} @target_parallel_depend_dynamic(
// CIR: omp.target depend(taskdependinout -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64))
// CIR: } {{.*}}cir.omp.depend_sizes_owned

// LLVM-LABEL: define{{.*}} @target_parallel_depend_section(
// LLVM: %[[SEC_SIZE:.+]] = mul i64 %{{.+}}, 4
// LLVM: store i64 %[[SEC_SIZE]], ptr %{{.+}}, align 8

// LLVM-LABEL: define{{.*}} @target_parallel_depend_dynamic(
// LLVM: %[[DYN_SIZE:.+]] = mul i64 %{{.+}}, 4
// LLVM: store i64 %[[DYN_SIZE]], ptr %{{.+}}, align 8

// OGCG-LABEL: define{{.*}} @target_parallel_depend_section(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(

// OGCG-LABEL: define{{.*}} @target_parallel_depend_dynamic(
// OGCG: call void @__kmpc_omp_taskwait_deps_51(
