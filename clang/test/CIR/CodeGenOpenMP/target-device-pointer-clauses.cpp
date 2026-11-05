// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

void sink(int *);

void target_is_device_ptr_ref(int *p) {
  int *&r = p;
  // CIR-LABEL: cir.func{{.*}} @_Z24target_is_device_ptr_refPi(
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "r"}
  // CIR: omp.target map_entries(%[[MAP]] -> %[[ARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
  // CIR: %[[PTR_SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["r"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[ARG]], %[[PTR_SLOT]]
  // CIR: %[[REF_SLOT:.*]] = cir.alloca !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!cir.ptr<!cir.ptr<!s32i>>>, ["r"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[PTR_SLOT]], %[[REF_SLOT]]
  // CIR: %[[REF_ADDR:.*]] = cir.load %[[REF_SLOT]] : !cir.ptr<!cir.ptr<!cir.ptr<!s32i>>>, !cir.ptr<!cir.ptr<!s32i>> {cir.nonnull, cir.ref_align = 8 : i64}
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[REF_ADDR]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @_Z4sinkPi(%[[VAL]]) : (!cir.ptr<!s32i>) -> ()
#pragma omp target is_device_ptr(r)
  sink(r);
}

// LLVM: @.offload_maptypes = private unnamed_addr constant [1 x i64] [i64 288]

// LLVM-LABEL: define dso_local void @_Z24target_is_device_ptr_refPi(
// LLVM: %[[HOST_REF:.*]] = load ptr, ptr %{{.*}}, align 8, !nonnull !{{[0-9]+}}, !align !{{[0-9]+}}
// LLVM: %[[HOST_VAL:.*]] = load ptr, ptr %[[HOST_REF]], align 8
// LLVM: call void @__omp_offloading{{.*}}_Z24target_is_device_ptr_refPi_l{{[0-9]+}}(ptr %[[HOST_VAL]])

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}_Z24target_is_device_ptr_refPi_l{{[0-9]+}}(
// LLVM: %[[PTR_SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[PTR_SLOT]], align 8
// LLVM: %[[REF_SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %[[PTR_SLOT]], ptr %[[REF_SLOT]], align 8
// LLVM: %[[REF_ADDR:.*]] = load ptr, ptr %[[REF_SLOT]], align 8, !nonnull !{{[0-9]+}}, !align !{{[0-9]+}}
// LLVM: %[[VAL:.*]] = load ptr, ptr %[[REF_ADDR]], align 8
// LLVM: call void @_Z4sinkPi(ptr noundef %[[VAL]])

// OGCG: @.offload_maptypes = private unnamed_addr constant [1 x i64] [i64 288]

// OGCG-LABEL: define dso_local void @_Z24target_is_device_ptr_refPi(
// OGCG: %[[HOST_REF:.*]] = load ptr, ptr %r, align 8, !nonnull !{{[0-9]+}}, !align !{{[0-9]+}}
// OGCG: %[[HOST_TMP:.*]] = load ptr, ptr %{{.*}}, align 8, !nonnull !{{[0-9]+}}, !align !{{[0-9]+}}
// OGCG: %[[HOST_VAL:.*]] = load ptr, ptr %[[HOST_TMP]], align 8
// OGCG: call void @__omp_offloading{{.*}}_Z24target_is_device_ptr_refPi_l{{[0-9]+}}(ptr %[[HOST_VAL]])

// OGCG-LABEL: define internal void @__omp_offloading{{.*}}_Z24target_is_device_ptr_refPi_l{{[0-9]+}}(
// OGCG: %[[PTR_SLOT:.*]] = alloca ptr, align 8
// OGCG: %[[REF_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %r, ptr %[[PTR_SLOT]], align 8
// OGCG: store ptr %[[PTR_SLOT]], ptr %[[REF_SLOT]], align 8
// OGCG: %[[REF_ADDR:.*]] = load ptr, ptr %[[REF_SLOT]], align 8, !nonnull !{{[0-9]+}}, !align !{{[0-9]+}}
// OGCG: %[[VAL:.*]] = load ptr, ptr %[[REF_ADDR]], align 8
// OGCG: call void @_Z4sinkPi(ptr noundef %[[VAL]])
