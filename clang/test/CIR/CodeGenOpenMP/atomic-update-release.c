// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify that the initial load in an atomic update cmpxchg loop uses
// monotonic ordering (not the full release ordering, which is invalid
// for loads). Only the cmpxchg itself should carry the release ordering.

_Bool bx;
long double ldv;

void atomic_update_release_bool(void) {
#pragma omp atomic update release
  bx = ldv * bx;
}

// CHECK-LABEL: define {{.*}} @atomic_update_release_bool(
// CHECK:         load atomic i8, ptr @bx monotonic
// CHECK:         cmpxchg ptr @bx, i8 %{{.*}}, i8 %{{.*}} release monotonic

// OGCG-LABEL: define {{.*}} @atomic_update_release_bool(
// OGCG:         load atomic i8, ptr @bx monotonic
// OGCG:         cmpxchg ptr @bx, i8 %{{.*}}, i8 %{{.*}} release monotonic

void atomic_update_release_ptr(int **x) {
#pragma omp atomic update release
  (*x)++;
}

// CHECK-LABEL: define {{.*}} @atomic_update_release_ptr(
// CHECK:         load atomic ptr, ptr %{{.*}} monotonic
// CHECK:         cmpxchg ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}} release monotonic

// OGCG-LABEL: define {{.*}} @atomic_update_release_ptr(
// OGCG:         load atomic ptr, ptr %{{.*}} monotonic
// OGCG:         cmpxchg ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}} release monotonic

int ix;
void atomic_update_release_mul(void) {
#pragma omp atomic update release
  ix *= 2;
}

// CHECK-LABEL: define {{.*}} @atomic_update_release_mul(
// CHECK:         load atomic i32, ptr @ix monotonic
// CHECK:         cmpxchg ptr @ix, i32 %{{.*}}, i32 %{{.*}} release monotonic

// OGCG-LABEL: define {{.*}} @atomic_update_release_mul(
// OGCG:         load atomic i32, ptr @ix monotonic
// OGCG:         cmpxchg ptr @ix, i32 %{{.*}}, i32 %{{.*}} release monotonic
