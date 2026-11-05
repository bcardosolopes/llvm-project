// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

int tick(void);
void sink(int *);

void plain(int *p) {
  // CIR-LABEL: cir.func{{.*}} @plain(
  // CIR: omp.target map_entries(%{{.*}} -> %arg{{[0-9]+}} : !cir.ptr<!cir.ptr<!s32i>>) private(@{{.*}} %{{.*}} -> %arg{{[0-9]+}} [map_idx=0] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: cir.call @sink
#pragma omp target
  sink(p);
}

void with_device(int *p) {
  // CIR-LABEL: cir.func{{.*}} @with_device(
  // CIR: cir.call @tick
  // CIR: omp.target device(%{{.*}} : i32) map_entries(%{{.*}} -> %arg{{[0-9]+}} : !cir.ptr<!cir.ptr<!s32i>>) private(@{{.*}} %{{.*}} -> %arg{{[0-9]+}} [map_idx=0] : !cir.ptr<!cir.ptr<!s32i>>) {
  // CIR: cir.call @sink
#pragma omp target device(tick())
  sink(p);
}

// LLVM-LABEL: define dso_local void @plain(
// LLVM: call void @__omp_offloading{{.*}}plain{{.*}}(ptr %{{.*}})
// LLVM-LABEL: define dso_local void @with_device(
// LLVM: call i32 @tick()
// LLVM: call void @__omp_offloading{{.*}}with_device{{.*}}(ptr %{{.*}})
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}plain{{.*}}(ptr{{.*}})
// LLVM: call void @sink(ptr noundef %{{.*}})
// LLVM-LABEL: define internal void @__omp_offloading{{.*}}with_device{{.*}}(ptr{{.*}})
// LLVM: call void @sink(ptr noundef %{{.*}})

// OGCG-LABEL: define dso_local void @plain(
// OGCG: call void @__omp_offloading{{.*}}plain{{.*}}(ptr %{{.*}})
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}plain{{.*}}(ptr{{.*}})
// OGCG: call void @sink(ptr noundef %{{.*}})

// OGCG-LABEL: define dso_local void @with_device(
// OGCG: call i32 @tick()
// OGCG: call void @__omp_offloading{{.*}}with_device{{.*}}(ptr %{{.*}})
// OGCG-LABEL: define internal void @__omp_offloading{{.*}}with_device{{.*}}(ptr{{.*}})
// OGCG: call void @sink(ptr noundef %{{.*}})
