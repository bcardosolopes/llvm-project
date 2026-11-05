// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int cond(void);
int device(void);
int extent(void);

void host_target_update_is_noop(int *a) {
  // CHECK-LABEL: cir.func{{.*}} @host_target_update_is_noop(
  // CHECK-NOT: cir.call @cond
  // CHECK-NOT: cir.call @device
  // CHECK-NOT: cir.call @extent
  // CHECK-NOT: omp.target_update
  // CHECK: cir.return
#pragma omp target update to(a[0:extent()]) if(cond()) device(device()) nowait
}

// LLVM-LABEL: define {{.*}} @host_target_update_is_noop(
// LLVM-NOT:     call {{.*}} @__tgt_target
// LLVM:         ret void

// OGCG-LABEL: define {{.*}} @host_target_update_is_noop(
// OGCG:         %a.addr = alloca ptr
// OGCG:         store ptr %a, ptr %a.addr
// OGCG:         ret void
