// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Comprehensive tests for OpenMP atomic operations covering various types,
// operations, and memory orderings.

// --- Atomic Read ---

void atomic_read_int(int *x, int *v) {
#pragma omp atomic read
  *v = *x;
}

// CHECK-LABEL: define {{.*}} @atomic_read_int(
// CHECK:         load atomic i32, ptr %{{.*}} monotonic
// OGCG-LABEL: define {{.*}} @atomic_read_int(
// OGCG:         load atomic i32, ptr %{{.*}} monotonic

void atomic_read_long(long *x, long *v) {
#pragma omp atomic read
  *v = *x;
}

// CHECK-LABEL: define {{.*}} @atomic_read_long(
// CHECK:         load atomic i64, ptr %{{.*}} monotonic
// OGCG-LABEL: define {{.*}} @atomic_read_long(
// OGCG:         load atomic i64, ptr %{{.*}} monotonic

// --- Atomic Write ---

void atomic_write_int(int *x) {
#pragma omp atomic write
  *x = 42;
}

// CHECK-LABEL: define {{.*}} @atomic_write_int(
// CHECK:         store atomic i32 42, ptr %{{.*}} monotonic
// OGCG-LABEL: define {{.*}} @atomic_write_int(
// OGCG:         store atomic i32 42, ptr %{{.*}} monotonic

// --- Atomic Update (RMW) ---

void atomic_update_add(int *x) {
#pragma omp atomic update
  *x += 1;
}

// CHECK-LABEL: define {{.*}} @atomic_update_add(
// CHECK:         atomicrmw add ptr %{{.*}}, i32 1 monotonic
// OGCG-LABEL: define {{.*}} @atomic_update_add(
// OGCG:         atomicrmw add ptr %{{.*}}, i32 1 monotonic

void atomic_update_sub(int *x) {
#pragma omp atomic update
  *x -= 5;
}

// CHECK-LABEL: define {{.*}} @atomic_update_sub(
// CHECK:         atomicrmw sub ptr %{{.*}}, i32 5 monotonic
// OGCG-LABEL: define {{.*}} @atomic_update_sub(
// OGCG:         atomicrmw sub ptr %{{.*}}, i32 5 monotonic

void atomic_update_and(unsigned *x) {
#pragma omp atomic update
  *x &= 0xFF;
}

// CHECK-LABEL: define {{.*}} @atomic_update_and(
// CHECK:         atomicrmw and ptr %{{.*}}, i32 255 monotonic
// OGCG-LABEL: define {{.*}} @atomic_update_and(
// OGCG:         atomicrmw and ptr %{{.*}}, i32 255 monotonic

void atomic_update_or(unsigned *x) {
#pragma omp atomic update
  *x |= 0xFF;
}

// CHECK-LABEL: define {{.*}} @atomic_update_or(
// CHECK:         atomicrmw or ptr %{{.*}}, i32 255 monotonic
// OGCG-LABEL: define {{.*}} @atomic_update_or(
// OGCG:         atomicrmw or ptr %{{.*}}, i32 255 monotonic

void atomic_update_xor(unsigned *x) {
#pragma omp atomic update
  *x ^= 0xFF;
}

// CHECK-LABEL: define {{.*}} @atomic_update_xor(
// CHECK:         atomicrmw xor ptr %{{.*}}, i32 255 monotonic
// OGCG-LABEL: define {{.*}} @atomic_update_xor(
// OGCG:         atomicrmw xor ptr %{{.*}}, i32 255 monotonic

// --- Atomic Update (cmpxchg fallback) ---

void atomic_update_mul(int *x) {
#pragma omp atomic update
  *x *= 2;
}

// CHECK-LABEL: define {{.*}} @atomic_update_mul(
// CHECK:         cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic
// OGCG-LABEL: define {{.*}} @atomic_update_mul(
// OGCG:         cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic

// --- Atomic Capture ---

void atomic_capture_add(int *x, int *v) {
  int tmp;
#pragma omp atomic capture
  { tmp = *x; *x += 1; }
  *v = tmp;
}

// CHECK-LABEL: define {{.*}} @atomic_capture_add(
// CHECK:         atomicrmw add ptr %{{.*}}, i32 1 monotonic
// OGCG-LABEL: define {{.*}} @atomic_capture_add(
// OGCG:         atomicrmw add ptr %{{.*}}, i32 1 monotonic

void atomic_capture_exchange(int *x, int *v) {
  int tmp;
#pragma omp atomic capture
  { tmp = *x; *x = 42; }
  *v = tmp;
}

// CHECK-LABEL: define {{.*}} @atomic_capture_exchange(
// CHECK:         atomicrmw xchg ptr %{{.*}}, i32 42 monotonic
// OGCG-LABEL: define {{.*}} @atomic_capture_exchange(
// OGCG:         atomicrmw xchg ptr %{{.*}}, i32 42 monotonic

// --- Memory Orderings ---

void atomic_read_acquire(int *x, int *v) {
#pragma omp atomic read acquire
  *v = *x;
}

// CHECK-LABEL: define {{.*}} @atomic_read_acquire(
// CHECK:         load atomic i32, ptr %{{.*}} acquire
// CHECK:         call void @__kmpc_flush(
// OGCG-LABEL: define {{.*}} @atomic_read_acquire(
// OGCG:         load atomic i32, ptr %{{.*}} acquire
// OGCG:         call void @__kmpc_flush(

void atomic_write_release(int *x) {
#pragma omp atomic write release
  *x = 42;
}

// CHECK-LABEL: define {{.*}} @atomic_write_release(
// CHECK:         store atomic i32 42, ptr %{{.*}} release
// CHECK:         call void @__kmpc_flush(
// OGCG-LABEL: define {{.*}} @atomic_write_release(
// OGCG:         store atomic i32 42, ptr %{{.*}} release
// OGCG:         call void @__kmpc_flush(

void atomic_update_seq_cst(int *x) {
#pragma omp atomic update seq_cst
  *x += 1;
}

// CHECK-LABEL: define {{.*}} @atomic_update_seq_cst(
// CHECK:         atomicrmw add ptr %{{.*}}, i32 1 seq_cst
// CHECK:         call void @__kmpc_flush(
// OGCG-LABEL: define {{.*}} @atomic_update_seq_cst(
// OGCG:         atomicrmw add ptr %{{.*}}, i32 1 seq_cst
// OGCG:         call void @__kmpc_flush(

// --- Pointer arithmetic ---

void atomic_ptr_update(int **x) {
#pragma omp atomic update
  (*x)++;
}

// CHECK-LABEL: define {{.*}} @atomic_ptr_update(
// CHECK:         cmpxchg ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}} monotonic monotonic
// OGCG-LABEL: define {{.*}} @atomic_ptr_update(
// OGCG:         cmpxchg ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}} monotonic monotonic
