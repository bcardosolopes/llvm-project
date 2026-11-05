// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void atomic_float_update_add(float *x) {
#pragma omp atomic update
  *x += 1.0f;
}

// CHECK-LABEL: define {{.*}} @atomic_float_update_add(
// CHECK:         atomicrmw fadd ptr %{{.*}}, float 1.000000e+00 monotonic

// OGCG-LABEL: define {{.*}} @atomic_float_update_add(
// OGCG:         atomicrmw fadd ptr %{{.*}}, float 1.000000e+00 monotonic

void atomic_float_update_sub(float *x) {
#pragma omp atomic update
  *x -= 1.0f;
}

// CHECK-LABEL: define {{.*}} @atomic_float_update_sub(
// CHECK:         atomicrmw fsub ptr %{{.*}}, float 1.000000e+00 monotonic

// OGCG-LABEL: define {{.*}} @atomic_float_update_sub(
// OGCG:         atomicrmw fsub ptr %{{.*}}, float 1.000000e+00 monotonic

void atomic_double_update_add(double *x) {
#pragma omp atomic update
  *x += 1.0;
}

// CHECK-LABEL: define {{.*}} @atomic_double_update_add(
// CHECK:         atomicrmw fadd ptr %{{.*}}, double 1.000000e+00 monotonic

// OGCG-LABEL: define {{.*}} @atomic_double_update_add(
// OGCG:         atomicrmw fadd ptr %{{.*}}, double 1.000000e+00 monotonic

void atomic_float_capture_add(float *x, float *v) {
  float tmp;
#pragma omp atomic capture
  { tmp = *x; *x += 1.0f; }
  *v = tmp;
}

// CHECK-LABEL: define {{.*}} @atomic_float_capture_add(
// CHECK:         atomicrmw fadd ptr %{{.*}}, float 1.000000e+00 monotonic

// OGCG-LABEL: define {{.*}} @atomic_float_capture_add(
// OGCG:         atomicrmw fadd ptr %{{.*}}, float 1.000000e+00 monotonic

void atomic_float_read(float *x, float *v) {
#pragma omp atomic read
  *v = *x;
}

// CHECK-LABEL: define {{.*}} @atomic_float_read(
// CHECK:         load atomic i32, ptr %{{.*}} monotonic

// OGCG-LABEL: define {{.*}} @atomic_float_read(
// OGCG:         load atomic float, ptr %{{.*}} monotonic

void atomic_float_write(float *x) {
#pragma omp atomic write
  *x = 1.0f;
}

// CHECK-LABEL: define {{.*}} @atomic_float_write(
// CHECK:         store atomic i32 1065353216, ptr %{{.*}} monotonic

// OGCG-LABEL: define {{.*}} @atomic_float_write(
// OGCG:         store atomic float 1.000000e+00, ptr %{{.*}} monotonic
