// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -fclangir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

double globalfloat;

void parallel_for_global_lastprivate(int *out) {
  int i;
#pragma omp parallel for lastprivate(globalfloat)
  for (i = 0; i < 4; ++i)
    globalfloat = i;
  *out = (int)globalfloat;
}

void parallel_for_simd_global_lastprivate(int *out) {
  int i;
#pragma omp parallel for simd lastprivate(globalfloat)
  for (i = 0; i < 4; ++i)
    globalfloat = i;
  *out = (int)globalfloat;
}

// CIR-LABEL: cir.func{{.*}} @parallel_for_global_lastprivate(
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
// CIR: %[[GLOBAL_SEED:[^ ]+]] = cir.get_global @globalfloat : !cir.ptr<!cir.double>
// CIR: %[[GLOBAL_COPY:[^ ]+]] = cir.get_global @globalfloat : !cir.ptr<!cir.double>
// CIR: omp.wsloop nowait private(@{{.*}} %[[GLOBAL_SEED]] -> %arg2 : !cir.ptr<!cir.double>) {
// CIR: cir.if %{{.+}} {
// CIR: %[[LAST_VAL:[^ ]+]] = cir.load align(8) %arg2 : !cir.ptr<!cir.double>, !cir.double
// CIR: cir.store align(8) %[[LAST_VAL]], %[[GLOBAL_COPY]] : !cir.double, !cir.ptr<!cir.double>
// CIR: %[[GLOBAL_AFTER:[^ ]+]] = cir.get_global @globalfloat : !cir.ptr<!cir.double>
// CIR: %[[FINAL_VAL:[^ ]+]] = cir.load align(8) %[[GLOBAL_AFTER]] : !cir.ptr<!cir.double>, !cir.double

// CIR-LABEL: cir.func{{.*}} @parallel_for_simd_global_lastprivate(
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
// CIR: %[[GLOBAL_SEED_SIMD:[^ ]+]] = cir.get_global @globalfloat : !cir.ptr<!cir.double>
// CIR: %[[GLOBAL_COPY_SIMD:[^ ]+]] = cir.get_global @globalfloat : !cir.ptr<!cir.double>
// CIR: omp.wsloop nowait private(@{{.*}} %[[GLOBAL_SEED_SIMD]] -> %arg2 : !cir.ptr<!cir.double>) {
// CIR: omp.simd
// CIR: cir.if %{{.+}} {
// CIR: %[[LAST_VAL_SIMD:[^ ]+]] = cir.load align(8) %arg2 : !cir.ptr<!cir.double>, !cir.double
// CIR: cir.store align(8) %[[LAST_VAL_SIMD]], %[[GLOBAL_COPY_SIMD]] : !cir.double, !cir.ptr<!cir.double>
// CIR: %[[GLOBAL_AFTER_SIMD:[^ ]+]] = cir.get_global @globalfloat : !cir.ptr<!cir.double>
// CIR: %[[FINAL_VAL_SIMD:[^ ]+]] = cir.load align(8) %[[GLOBAL_AFTER_SIMD]] : !cir.ptr<!cir.double>, !cir.double

// OGCG-LABEL: define dso_local void @parallel_for_global_lastprivate(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @parallel_for_global_lastprivate.omp_outlined

// OGCG-LABEL: define internal void @parallel_for_global_lastprivate.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @parallel_for_simd_global_lastprivate(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 2, ptr @parallel_for_simd_global_lastprivate.omp_outlined

// OGCG-LABEL: define internal void @parallel_for_simd_global_lastprivate.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
