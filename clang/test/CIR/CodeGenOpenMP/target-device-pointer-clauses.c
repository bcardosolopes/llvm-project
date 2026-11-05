// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple nvptx64-nvidia-cuda -fopenmp -fopenmp-version=51 -fopenmp-targets=nvptx64-nvidia-cuda -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=NVPTXLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple nvptx64-nvidia-cuda -fopenmp -fopenmp-version=51 -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=NVPTXOGCG

void sink(int *);
int *g;

void target_is_device_ptr(int *p) {
  // CIR-LABEL: cir.func{{.*}} @target_is_device_ptr(
  // CIR: %[[VAL:.*]] = cir.load align(8) %{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%[[VAL]] : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "p"}
  // CIR: omp.target map_entries(%[[MAP]] -> %[[ARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
  // CIR: %[[SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[ARG]], %[[SLOT]]
  // CIR: cir.call @sink
#pragma omp target is_device_ptr(p)
  sink(p);
}

void target_has_device_addr(int *p) {
  // CIR-LABEL: cir.func{{.*}} @target_has_device_addr(
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "p"}
  // CIR: omp.target has_device_addr(%[[MAP]] -> %[[ARG:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[ARG]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink
#pragma omp target has_device_addr(p)
  sink(p);
}

void target_is_device_ptr_global(void) {
  // CIR-LABEL: cir.func{{.*}} @target_is_device_ptr_global(
  // CIR: %[[G_ADDR:.*]] = cir.get_global @g : !cir.ptr<!cir.ptr<!s32i>>
  // CIR: %[[G_VAL:.*]] = cir.load align(8) %[[G_ADDR]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%[[G_VAL]] : !cir.ptr<!s32i>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!s32i> {name = "g"}
  // CIR: omp.target map_entries(%[[MAP]] -> %[[ARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
  // CIR: %[[SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["g"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[ARG]], %[[SLOT]]
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink
#pragma omp target is_device_ptr(g)
  sink(g);
}

void target_has_device_addr_global(void) {
  // CIR-LABEL: cir.func{{.*}} @target_has_device_addr_global(
  // CIR: %[[G_ADDR:.*]] = cir.get_global @g : !cir.ptr<!cir.ptr<!s32i>>
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%[[G_ADDR]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(is_device_ptr) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "g"}
  // CIR: omp.target has_device_addr(%[[MAP]] -> %[[ARG:arg[0-9]+]] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: %[[VAL:.*]] = cir.load align(8) %[[ARG]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: cir.call @sink
#pragma omp target has_device_addr(g)
  sink(g);
}

void target_has_device_addr_section(int *p) {
  // CIR-LABEL: cir.func{{.*}} @target_has_device_addr_section(
  // CIR: %[[BOUND:.*]] = omp.map.bounds lower_bound(%{{.*}} : i64) upper_bound(%{{.*}} : i64) extent(%{{.*}} : i64) stride(%{{.*}} : i64) start_idx(%{{.*}} : i64)
  // CIR: %[[VAL:.*]] = cir.load align(8) %{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: %[[MAP:.*]] = omp.map.info var_ptr(%[[VAL]] : !cir.ptr<!s32i>, !s32i) map_clauses(is_device_ptr) capture(ByRef) bounds(%[[BOUND]]) -> !cir.ptr<!s32i> {name = "p[1:3]"}
  // CIR: omp.target has_device_addr(%[[MAP]] -> %[[ARG:arg[0-9]+]] : !cir.ptr<!s32i>) {
  // CIR: %[[SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p"] {alignment = 8 : i64}
  // CIR: cir.store align(8) %[[ARG]], %[[SLOT]]
  // CIR: %[[PTR:.*]] = cir.load align(8) %[[SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
  // CIR: %[[ADDR:.*]] = cir.ptr_stride inbounds %[[PTR]], %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
  // CIR: cir.call @sink(%[[ADDR]])
#pragma omp target has_device_addr(p[1:3])
  sink(p + 1);
}

// LLVM: @.offload_sizes = private unnamed_addr constant [1 x i64] [i64 8]
// LLVM: @.offload_maptypes = private unnamed_addr constant [1 x i64] [i64 288]

// LLVM-LABEL: define dso_local void @target_is_device_ptr(
// LLVM: %[[HOST_SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[HOST_SLOT]], align 8
// LLVM: %[[HOST_VAL:.*]] = load ptr, ptr %[[HOST_SLOT]], align 8
// LLVM: call void @__omp_offloading{{.*}}target_is_device_ptr{{.*}}(ptr %[[HOST_VAL]])
// LLVM-LABEL: define dso_local void @target_has_device_addr(
// LLVM: %[[HOST_SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[HOST_SLOT]], align 8
// LLVM: call void @__omp_offloading{{.*}}target_has_device_addr{{.*}}(ptr %[[HOST_SLOT]])
// LLVM-LABEL: define dso_local void @target_is_device_ptr_global(
// LLVM: %[[HOST_VAL:.*]] = load ptr, ptr @g, align 8
// LLVM: call void @__omp_offloading{{.*}}target_is_device_ptr_global{{.*}}(ptr %[[HOST_VAL]])
// LLVM-LABEL: define dso_local void @target_has_device_addr_global(
// LLVM: call void @__omp_offloading{{.*}}target_has_device_addr_global{{.*}}(ptr @g)
// LLVM-LABEL: define dso_local void @target_has_device_addr_section(
// LLVM: call void @__omp_offloading{{.*}}target_has_device_addr_section{{.*}}(ptr %{{.*}})

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_is_device_ptr{{.*}}(
// LLVM: %[[SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[SLOT]], align 8
// LLVM: %[[VAL:.*]] = load ptr, ptr %[[SLOT]], align 8
// LLVM: call void @sink(ptr noundef %[[VAL]])

// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}})
// LLVM: %[[VAL:.*]] = load ptr, ptr %0, align 8
// LLVM: call void @sink(ptr noundef %[[VAL]])
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_is_device_ptr_global{{.*}}(
// LLVM: %[[SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[SLOT]], align 8
// LLVM: %[[VAL:.*]] = load ptr, ptr %[[SLOT]], align 8
// LLVM: call void @sink(ptr noundef %[[VAL]])
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_global{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}})
// LLVM: %[[VAL:.*]] = load ptr, ptr %0, align 8
// LLVM: call void @sink(ptr noundef %[[VAL]])
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_section{{.*}}(ptr noundef %{{.*}})
// LLVM: %[[SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM: store ptr %0, ptr %[[SLOT]], align 8
// LLVM: %[[VAL:.*]] = load ptr, ptr %[[SLOT]], align 8
// LLVM: %[[NEXT:.*]] = getelementptr inbounds i32, ptr %[[VAL]], i64 1
// LLVM: call void @sink(ptr noundef %[[NEXT]])

// OGCG: @.offload_sizes = private unnamed_addr constant [1 x i64] [i64 8]
// OGCG: @.offload_maptypes = private unnamed_addr constant [1 x i64] [i64 288]

// OGCG-LABEL: define dso_local void @target_is_device_ptr(
// OGCG: %[[HOST_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %p, ptr %[[HOST_SLOT]], align 8
// OGCG: %[[HOST_VAL:.*]] = load ptr, ptr %[[HOST_SLOT]], align 8
// OGCG: call void @__omp_offloading{{.*}}target_is_device_ptr{{.*}}(ptr %[[HOST_VAL]])
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_is_device_ptr{{.*}}(
// OGCG: %[[ARG_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %p, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[ARG_VAL:.*]] = load ptr, ptr %[[ARG_SLOT]], align 8
// OGCG: call void @sink(ptr noundef %[[ARG_VAL]])

// OGCG-LABEL: define dso_local void @target_has_device_addr(
// OGCG: %[[HOST_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %p, ptr %[[HOST_SLOT]], align 8
// OGCG: call void @__omp_offloading{{.*}}target_has_device_addr{{.*}}(ptr %[[HOST_SLOT]])
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}})
// OGCG: %[[ARG_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %p, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[ADDR:.*]] = load ptr, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[VAL:.*]] = load ptr, ptr %[[ADDR]], align 8
// OGCG: call void @sink(ptr noundef %[[VAL]])
// OGCG-LABEL: define dso_local void @target_is_device_ptr_global(
// OGCG: %[[HOST_VAL:.*]] = load ptr, ptr @g, align 8
// OGCG: call void @__omp_offloading{{.*}}target_is_device_ptr_global{{.*}}(ptr %[[HOST_VAL]])
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_is_device_ptr_global{{.*}}(
// OGCG: %[[ARG_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %g, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[ARG_VAL:.*]] = load ptr, ptr %[[ARG_SLOT]], align 8
// OGCG: call void @sink(ptr noundef %[[ARG_VAL]])
// OGCG-LABEL: define dso_local void @target_has_device_addr_global(
// OGCG: call void @__omp_offloading{{.*}}target_has_device_addr_global{{.*}}(ptr @g)
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_global{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}})
// OGCG: %[[ARG_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %g, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[ADDR:.*]] = load ptr, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[VAL:.*]] = load ptr, ptr %[[ADDR]], align 8
// OGCG: call void @sink(ptr noundef %[[VAL]])
// OGCG-LABEL: define dso_local void @target_has_device_addr_section(
// OGCG: call void @__omp_offloading{{.*}}target_has_device_addr_section{{.*}}(ptr %{{.*}})
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_section{{.*}}(ptr noundef %{{.*}})
// OGCG: %[[ARG_SLOT:.*]] = alloca ptr, align 8
// OGCG: store ptr %p, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[ARG_VAL:.*]] = load ptr, ptr %[[ARG_SLOT]], align 8
// OGCG: %[[NEXT:.*]] = getelementptr inbounds i32, ptr %[[ARG_VAL]], i64 1
// OGCG: call void @sink(ptr noundef %[[NEXT]])

// NVPTXLLVM-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// NVPTXLLVM-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_global{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// NVPTXLLVM-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_section{{.*}}(ptr noundef %{{.*}}) #{{[0-9]+}} {

// NVPTXOGCG-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// NVPTXOGCG-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_global{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) #{{[0-9]+}} {
// NVPTXOGCG-LABEL: define internal void @__omp_offloading{{.*}}target_has_device_addr_section{{.*}}(ptr noundef %{{.*}}) #{{[0-9]+}} {
