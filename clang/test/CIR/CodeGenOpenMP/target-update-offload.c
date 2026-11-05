// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-vla -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-vla -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-vla -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int device(void);
double gc[100];

void offload_scalar(int arg) {
  // CIR-LABEL: cir.func{{.*}} @offload_scalar(
  // CIR: %[[MAP:.*]] = omp.map.info
  // CIR-SAME: map_clauses(to)
  // CIR-SAME: capture(ByRef)
  // CIR: omp.target_update {{.*}}device(%{{.*}} : i32) {{.*}}if(%{{.*}}) {{.*}}map_entries(%[[MAP]] : !cir.ptr<!s32i>)
#pragma omp target update to(arg) if(arg) device(4)
  ;
}

void offload_nowait_depend(int dep) {
  // CIR-LABEL: cir.func{{.*}} @offload_nowait_depend(
  // CIR: %[[MAP:.*]] = omp.map.info
  // CIR-SAME: map_clauses(from)
  // CIR-SAME: capture(ByRef)
  // CIR: omp.target_update {{.*}}depend(taskdependin -> %{{.*}} : !cir.ptr<!s32i> size(%{{.*}} : i64)) {{.*}}device(%{{.*}} : i32) {{.*}}map_entries(%[[MAP]] : !cir.ptr<!cir.array<!cir.double x 100>>) nowait
#pragma omp target update if(1 + 3 - 5) device(device()) from(gc) nowait depend(in : dep)
  ;
}

void offload_vla(int n) {
  float lb[n];
  // CIR-LABEL: cir.func{{.*}} @offload_vla(
  // CIR: %[[BOUND:.*]] = omp.map.bounds
  // CIR: %[[MAP:.*]] = omp.map.info
  // CIR-SAME: map_clauses(from)
  // CIR-SAME: bounds(%[[BOUND]])
  // CIR: omp.target_update
  // CIR-SAME: map_entries(%[[MAP]]
#pragma omp target update from(lb)
  ;
}

// LLVM-LABEL: define{{.*}} @offload_scalar(
// LLVM: br i1
// LLVM: call void @__tgt_target_data_update_mapper(ptr @{{.*}}, i64 4, i32 1,

// LLVM-LABEL: define{{.*}} @offload_nowait_depend(
// LLVM: call i32 @device()
// LLVM: call ptr @__kmpc_omp_target_task_alloc(
// LLVM: call i32 @__kmpc_omp_task_with_deps(
// LLVM: call void @__tgt_target_data_update_nowait_mapper(

// LLVM-LABEL: define{{.*}} @offload_vla(
// LLVM: call void @__tgt_target_data_update_mapper(

// OGCG-LABEL: define{{.*}} @offload_scalar(
// OGCG:         call void @__tgt_target_data_update_mapper(

// OGCG-LABEL: define{{.*}} @offload_nowait_depend(
// OGCG:         call void @__tgt_target_data_update_nowait_mapper(

// OGCG-LABEL: define{{.*}} @offload_vla(
// OGCG:         call void @__tgt_target_data_update_mapper(
