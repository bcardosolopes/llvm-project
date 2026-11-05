// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void taskloop_if_grainsize(int n) {
#pragma omp taskloop if(n) nogroup grainsize(n)
  for (int i = 0; i < 4; ++i) {
  }
}

void taskloop_num_tasks_bundle(int n) {
#pragma omp taskloop final(n) mergeable num_tasks(n) priority(n) untied
  for (int i = 0; i < 4; ++i) {
  }
}

void taskloop_shared_clause(int n) {
#pragma omp taskloop shared(n)
  for (int i = 0; i < 4; ++i) {
  }
}

void taskloop_private_clause(int n) {
#pragma omp taskloop private(n)
  for (int i = 0; i < 4; ++i) {
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_if_grainsizei(
// CIR: omp.taskloop grainsize(%{{.*}}) if(%{{.*}}) nogroup private(@{{.*}} %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_num_tasks_bundlei(
// CIR: omp.taskloop final(%{{.*}}) mergeable num_tasks(%{{.*}}) priority(%{{.*}}) untied private(@{{.*}} %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_shared_clausei(
// CIR: omp.taskloop private(@{{.*}} %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}taskloop_private_clausei(
// CIR: omp.taskloop private(@{{.*}} %{{.*}} -> %arg{{.*}}, @{{.*}} %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) {

// OGCG-LABEL: define {{.*}} @_Z21taskloop_if_grainsizei(
// OGCG: call ptr @__kmpc_omp_task_alloc(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z25taskloop_num_tasks_bundlei(
// OGCG: call void @__kmpc_taskgroup(
// OGCG: call ptr @__kmpc_omp_task_alloc(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z22taskloop_shared_clausei(
// OGCG: call ptr @__kmpc_omp_task_alloc(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z23taskloop_private_clausei(
// OGCG: call ptr @__kmpc_omp_task_alloc(
// OGCG: call void @__kmpc_taskloop(
