// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=OMP50
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=OMP51
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG50
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG51

// Verify that atomic capture with acq_rel emits a flush call for OpenMP < 5.1
// and suppresses it for OpenMP >= 5.1.

void atomic_capture_acq_rel(int *x, int *v) {
  int tmp;
#pragma omp atomic capture acq_rel
  { tmp = *x; *x = *x + 1; }
  *v = tmp;
}

// OMP50-LABEL: define {{.*}} @atomic_capture_acq_rel(
// OMP50:         atomicrmw add
// OMP50:         call void @__kmpc_flush(

// OMP51-LABEL: define {{.*}} @atomic_capture_acq_rel(
// OMP51:         atomicrmw add
// OMP51-NOT:     call void @__kmpc_flush(

// OGCG50-LABEL: define {{.*}} @atomic_capture_acq_rel(
// OGCG50:         atomicrmw add
// OGCG50:         call void @__kmpc_flush(

// OGCG51-LABEL: define {{.*}} @atomic_capture_acq_rel(
// OGCG51:         atomicrmw add
// OGCG51-NOT:     call void @__kmpc_flush(

void atomic_capture_acquire(int *x, int *v) {
  int tmp;
#pragma omp atomic capture acquire
  { tmp = *x; *x = *x + 1; }
  *v = tmp;
}

// OMP50-LABEL: define {{.*}} @atomic_capture_acquire(
// OMP50:         atomicrmw add
// OMP50:         call void @__kmpc_flush(

// OMP51-LABEL: define {{.*}} @atomic_capture_acquire(
// OMP51:         atomicrmw add
// OMP51-NOT:     call void @__kmpc_flush(

// OGCG50-LABEL: define {{.*}} @atomic_capture_acquire(
// OGCG50:         atomicrmw add
// OGCG50:         call void @__kmpc_flush(

// OGCG51-LABEL: define {{.*}} @atomic_capture_acquire(
// OGCG51:         atomicrmw add
// OGCG51-NOT:     call void @__kmpc_flush(

// Verify that atomic read still emits flush regardless of version.
void atomic_read_acquire(int *x, int *v) {
#pragma omp atomic read acquire
  *v = *x;
}

// OMP50-LABEL: define {{.*}} @atomic_read_acquire(
// OMP50:         call void @__kmpc_flush(

// OMP51-LABEL: define {{.*}} @atomic_read_acquire(
// OMP51:         call void @__kmpc_flush(

// OGCG50-LABEL: define {{.*}} @atomic_read_acquire(
// OGCG50:         call void @__kmpc_flush(

// OGCG51-LABEL: define {{.*}} @atomic_read_acquire(
// OGCG51:         call void @__kmpc_flush(
