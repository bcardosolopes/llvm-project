// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void sink(int *);

void target_teams_is_device_ptr_host_only(int *p) {
  // HOST-LABEL: cir.func{{.*}} @target_teams_is_device_ptr_host_only(
  // HOST: omp.teams {
  // HOST: cir.call @sink
#pragma omp target teams is_device_ptr(p)
  sink(p);
}

void target_teams_has_device_addr_host_only(int *p) {
  // HOST-LABEL: cir.func{{.*}} @target_teams_has_device_addr_host_only(
  // HOST: omp.teams {
  // HOST: cir.call @sink
#pragma omp target teams has_device_addr(p)
  sink(p);
}

// OGCG-LABEL: define dso_local void @target_teams_is_device_ptr_host_only(
// OGCG: call void @__omp_offloading_{{.*}}_target_teams_is_device_ptr_host_only_{{.*}}(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_is_device_ptr_host_only_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @target_teams_has_device_addr_host_only(
// OGCG: call void @__omp_offloading_{{.*}}_target_teams_has_device_addr_host_only_{{.*}}(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_target_teams_has_device_addr_host_only_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(
