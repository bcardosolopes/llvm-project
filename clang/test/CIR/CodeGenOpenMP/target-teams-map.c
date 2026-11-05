// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST-CIR
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST-LLVM --implicit-check-not="call i32 @extent"
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=OFFLOAD-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=OFFLOAD-LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG --implicit-check-not="call i32 @extent"

int extent(void);
void work(int *);

void host_map(int *a) {
  // HOST-CIR-LABEL: cir.func{{.*}} @host_map
  // HOST-CIR: omp.teams
  // HOST-CIR: cir.call @work

  // HOST-LLVM-LABEL: define dso_local void @host_map(ptr noundef %{{.*}})
  // HOST-LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 1, ptr @host_map..omp_par, ptr %{{.*}})
  // HOST-LLVM-LABEL: define internal void @host_map..omp_par(ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}})
  // HOST-LLVM: call void @work(ptr noundef %{{.*}})

  // OFFLOAD-CIR-LABEL: cir.func{{.*}} @host_map
  // OFFLOAD-CIR: omp.target map_entries(%[[MAP:.*]] -> %arg1 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: %[[SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["a"]
  // OFFLOAD-CIR: cir.store align(8) %arg1, %[[SLOT]]
  // OFFLOAD-CIR: omp.teams {
  // OFFLOAD-CIR: cir.call @work

  // OFFLOAD-LLVM-LABEL: define dso_local void @host_map(ptr noundef %{{.*}})
  // OFFLOAD-LLVM: call i32 @__tgt_target_kernel(
#pragma omp target teams map(tofrom : a[0:4])
  work(a);
}

void host_map_extent_ignored(int *a) {
  // HOST-CIR-LABEL: cir.func{{.*}} @host_map_extent_ignored
  // HOST-CIR-NOT: cir.call @extent
  // HOST-CIR: omp.teams
  // HOST-CIR: cir.call @work

  // HOST-LLVM-LABEL: define dso_local void @host_map_extent_ignored(ptr noundef %{{.*}})
  // HOST-LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 1, ptr @host_map_extent_ignored..omp_par, ptr %{{.*}})
  // HOST-LLVM-LABEL: define internal void @host_map_extent_ignored..omp_par(ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}})
  // HOST-LLVM: call void @work(ptr noundef %{{.*}})

  // OFFLOAD-CIR-LABEL: cir.func{{.*}} @host_map_extent_ignored
  // OFFLOAD-CIR: cir.call @extent
  // OFFLOAD-CIR: omp.target map_entries(%[[MAP:.*]] -> %arg1 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: omp.teams {
  // OFFLOAD-CIR: cir.call @work

  // OFFLOAD-LLVM-LABEL: define dso_local void @host_map_extent_ignored(ptr noundef %{{.*}})
  // OFFLOAD-LLVM: call i32 @extent()
  // OFFLOAD-LLVM: call i32 @__tgt_target_kernel(
  // OFFLOAD-LLVM-LABEL: define internal void @__omp_offloading{{.*}}host_map_extent_ignored{{.*}}(ptr %0)
  // OFFLOAD-LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}host_map_extent_ignored{{.*}}.omp_par, ptr %{{.*}})
#pragma omp target teams map(tofrom : a[0:extent()])
  work(a);
}

// OGCG-LABEL: define dso_local void @host_map(
// OGCG: call void @__omp_offloading_{{.*}}_host_map_{{.*}}(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_host_map_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @host_map_extent_ignored(
// OGCG: call void @__omp_offloading_{{.*}}_host_map_extent_ignored_{{.*}}(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_host_map_extent_ignored_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
