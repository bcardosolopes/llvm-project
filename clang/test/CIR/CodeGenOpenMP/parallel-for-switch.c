// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify that `switch` inside an OpenMP parallel for body compiles
// without crashing.  Switch flattening can leave unreachable dead blocks
// inside the omp.loop_nest region; these must be erased before
// OpenMPIRBuilder outlining.

void switch_in_parallel_for(int *out) {
  #pragma omp parallel for
  for (int i = 0; i < 100; i++) {
    switch (i) {
    case 50:
      out[i] = 50;
      break;
    default:
      out[i] = 0;
      break;
    }
  }
}

// CIR-LABEL: cir.func{{.*}} @switch_in_parallel_for(
// CIR: omp.loop_nest
// CIR: cir.do {
// CIR:   cir.scope {
// CIR:     cir.switch
// CIR:       cir.break
// CIR:       cir.break
// CIR:   }
// CIR: } while {
// CIR:   %{{.+}} = cir.const #false
// CIR:   cir.condition(%{{.+}})
// CIR: }
// CIR: omp.yield

// LLVM-LABEL: define {{.*}}void @switch_in_parallel_for..omp_par(
// LLVM: call void @__kmpc_for_static_init_4
// LLVM: switch i32 %{{.+}}, label %{{.+}} [
// LLVM:   i32 50, label %{{.+}}
// LLVM: ]

// Multiple cases with continue + switch
void switch_and_continue(int *out) {
  #pragma omp parallel for
  for (int i = 0; i < 100; i++) {
    if (i < 10) continue;
    switch (i % 3) {
    case 0: out[i] = 0; break;
    case 1: out[i] = 1; break;
    default: out[i] = 2; break;
    }
  }
}

// CIR-LABEL: cir.func{{.*}} @switch_and_continue(
// CIR: omp.loop_nest
// CIR: cir.do {
// CIR:   cir.scope {
// CIR:     cir.if %{{.+}} {
// CIR:       cir.continue
// CIR:     }
// CIR:     cir.switch
// CIR:   }
// CIR: } while {
// CIR:   %{{.+}} = cir.const #false
// CIR: }
// CIR: omp.yield

// LLVM-LABEL: define {{.*}}void @switch_and_continue..omp_par(
// LLVM: call void @__kmpc_for_static_init_4
// LLVM: switch i32 %{{.+}}, label %{{.+}} [

// OGCG-LABEL: define dso_local void @switch_in_parallel_for(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @switch_in_parallel_for.omp_outlined

// OGCG-LABEL: define internal void @switch_in_parallel_for.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: switch i32 %{{.+}}, label %{{.+}} [
// OGCG:   i32 50, label %{{.+}}
// OGCG: ]
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @switch_and_continue(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @switch_and_continue.omp_outlined

// OGCG-LABEL: define internal void @switch_and_continue.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: switch i32 %{{.+}}, label %{{.+}} [
// OGCG: call void @__kmpc_for_static_fini(
