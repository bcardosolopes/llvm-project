// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int cond(void);
int device(void);
int extent(void);
void work(int *);

void host_target_data_emits_body(int *a) {
  // CHECK-LABEL: cir.func{{.*}} @host_target_data_emits_body(
  // CHECK-NOT: cir.call @cond
  // CHECK-NOT: cir.call @device
  // CHECK-NOT: cir.call @extent
  // CHECK-NOT: omp.target_data
  // CHECK: cir.call @work
  // CHECK: cir.return
#pragma omp target data map(tofrom: a[0:extent()]) if(cond()) device(device())
  work(a);
}

void host_target_data_use_device_ptr(int *a) {
  // CHECK-LABEL: cir.func{{.*}} @host_target_data_use_device_ptr(
  // CHECK-NOT: omp.target_data
  // CHECK: cir.call @work
  // CHECK: cir.return
#pragma omp target data use_device_ptr(a)
  work(a);
}

void host_target_data_use_device_addr(int *a) {
  // CHECK-LABEL: cir.func{{.*}} @host_target_data_use_device_addr(
  // CHECK-NOT: omp.target_data
  // CHECK: cir.call @work
  // CHECK: cir.return
#pragma omp target data use_device_addr(a)
  work(a);
}

// LLVM-LABEL: define {{.*}} @host_target_data_emits_body(
// LLVM:         call void @work(
// LLVM-LABEL: define {{.*}} @host_target_data_use_device_ptr(
// LLVM:         call void @work(
// LLVM-LABEL: define {{.*}} @host_target_data_use_device_addr(
// LLVM:         call void @work(

// OGCG-LABEL: define {{.*}} @host_target_data_emits_body(
// OGCG:         call void @work(
// OGCG-LABEL: define {{.*}} @host_target_data_use_device_ptr(
// OGCG:         call void @work(
// OGCG-LABEL: define {{.*}} @host_target_data_use_device_addr(
// OGCG:         call void @work(
