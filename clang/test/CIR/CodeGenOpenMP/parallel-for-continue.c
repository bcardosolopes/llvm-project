// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify that `continue` inside an OpenMP parallel for body compiles
// without crashing.  The body is wrapped in a do_while(false) so that
// cir.continue has a valid CIR LoopOpInterface parent.

void continue_in_parallel_for(int *out) {
  #pragma omp parallel for
  for (int i = 0; i < 100; i++) {
    if (i > 50) continue;
    out[i] = i;
  }
}

// CIR-LABEL: cir.func{{.*}} @continue_in_parallel_for(
// CIR: omp.loop_nest
// CIR: cir.do {
// CIR:   cir.scope {
// CIR:     cir.if %{{.+}} {
// CIR:       cir.continue
// CIR:     }
// CIR:   }
// CIR:   cir.yield
// CIR: } while {
// CIR:   %{{.+}} = cir.const #false
// CIR:   cir.condition(%{{.+}})
// CIR: }
// CIR: omp.yield

// LLVM-LABEL: define {{.*}}void @continue_in_parallel_for..omp_par(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: br i1 false
// LLVM: call void @__kmpc_for_static_fini

void continue_in_for(int *out) {
  #pragma omp for
  for (int i = 0; i < 100; i++) {
    if (i > 50) continue;
    out[i] = i;
  }
}

// CIR-LABEL: cir.func{{.*}} @continue_in_for(
// CIR: omp.loop_nest
// CIR: cir.do {
// CIR:   cir.scope {
// CIR:     cir.if %{{.+}} {
// CIR:       cir.continue
// CIR:     }
// CIR:   }
// CIR: } while {
// CIR:   %{{.+}} = cir.const #false
// CIR: }
// CIR: omp.yield

// Verify that continue inside a nested regular loop still targets the
// inner loop (not the OpenMP wrapper do-while).
void continue_in_nested_loop(int *out) {
  #pragma omp parallel for
  for (int i = 0; i < 100; i++) {
    for (int j = 0; j < 10; j++) {
      if (j > 5) continue;
      out[i * 10 + j] = i + j;
    }
  }
}

// CIR-LABEL: cir.func{{.*}} @continue_in_nested_loop
// The inner for loop has its own cir.for which owns the continue.
// CIR: omp.loop_nest
// CIR: cir.do {
// CIR:   cir.scope {
// CIR:     cir.for
// CIR:       cir.continue

// OGCG-LABEL: define dso_local void @continue_in_parallel_for(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @continue_in_parallel_for.omp_outlined

// OGCG-LABEL: define internal void @continue_in_parallel_for.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @continue_in_for(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @continue_in_nested_loop(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @continue_in_nested_loop.omp_outlined

// OGCG-LABEL: define internal void @continue_in_nested_loop.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
