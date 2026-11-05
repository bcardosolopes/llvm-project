// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sink(int *);

void target_teams_is_device_ptr_param(int *p) {
  // CIR-LABEL: cir.func{{.*}} @target_teams_is_device_ptr_param(
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "p"}
  // CIR: omp.target map_entries(%[[MAP]] -> %[[TARGET_ARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
  // CIR: %[[TARGET_SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[TARGET_ARG]], %[[TARGET_SLOT]]
  // CIR: omp.teams private(@{{.*}} %[[TARGET_SLOT]] -> %[[TEAMS_SLOT:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[TEAMS_SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink(%[[VAL]])
#pragma omp target teams is_device_ptr(p)
  sink(p);
}

void target_teams_has_device_addr_param(int *p) {
  // CIR-LABEL: cir.func{{.*}} @target_teams_has_device_addr_param(
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "p"}
  // CIR: omp.target has_device_addr(%[[MAP]] -> %[[TARGET_ARG:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: omp.teams private(@{{.*}} %[[TARGET_ARG]] -> %[[TEAMS_SLOT:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[TEAMS_SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink(%[[VAL]])
#pragma omp target teams has_device_addr(p)
  sink(p);
}

void target_teams_is_device_ptr_local(int *arg) {
  int *p = arg;

  // CIR-LABEL: cir.func{{.*}} @target_teams_is_device_ptr_local(
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "p"}
  // CIR: omp.target map_entries(%[[MAP]] -> %[[TARGET_ARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
  // CIR: %[[TARGET_SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[TARGET_ARG]], %[[TARGET_SLOT]]
  // CIR: omp.teams private(@{{.*}} %[[TARGET_SLOT]] -> %[[TEAMS_SLOT:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[TEAMS_SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink(%[[VAL]])
#pragma omp target teams is_device_ptr(p)
  sink(p);
}

void target_teams_has_device_addr_local(int *arg) {
  int *p = arg;

  // CIR-LABEL: cir.func{{.*}} @target_teams_has_device_addr_local(
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "p"}
  // CIR: omp.target has_device_addr(%[[MAP]] -> %[[TARGET_ARG:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: omp.teams private(@{{.*}} %[[TARGET_ARG]] -> %[[TEAMS_SLOT:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[TEAMS_SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink(%[[VAL]])
#pragma omp target teams has_device_addr(p)
  sink(p);
}

// OGCG-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 288]

// OGCG-LABEL: define dso_local void @target_teams_is_device_ptr_param(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_is_device_ptr_param_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @target_teams_has_device_addr_param(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_has_device_addr_param_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @target_teams_is_device_ptr_local(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_is_device_ptr_local_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @target_teams_has_device_addr_local(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_has_device_addr_local_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
