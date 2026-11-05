// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic update of non-trivial types. The OMPIRBuilder lowers these
// through __atomic_load + __atomic_compare_exchange libcall loops. Verify the
// size parameter matches the alloc size (e.g. 16 for complex double, 8 for
// complex float, 16 for long double / x86_fp80).

_Complex double cdx, cde;

void test_complex_double_update(void) {
#pragma omp atomic update
  cdx += cde;
}

// CHECK-LABEL: define {{.*}} @test_complex_double_update()
// CHECK:         call void @__atomic_load(i64 16, ptr @cdx,
// CHECK:         call i1 @__atomic_compare_exchange(i64 16, ptr @cdx,

_Complex float cfx, cfe;

void test_complex_float_update(void) {
#pragma omp atomic update
  cfx += cfe;
}

// CHECK-LABEL: define {{.*}} @test_complex_float_update()
// CHECK:         call void @__atomic_load(i64 8, ptr @cfx,
// CHECK:         call i1 @__atomic_compare_exchange(i64 8, ptr @cfx,

// x86_fp80 has store size 10 (non-power-of-2), alloc size 16.
long double ldx;

void test_long_double_update(void) {
#pragma omp atomic update
  ldx += 1.0L;
}

// CHECK-LABEL: define {{.*}} @test_long_double_update()
// CHECK:         call void @__atomic_load(i64 16, ptr @ldx,
// CHECK:         call i1 @__atomic_compare_exchange(i64 16, ptr @ldx,

// OGCG-LABEL: define {{.*}} @test_complex_double_update()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @cdx,
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 16, ptr noundef @cdx,

// OGCG-LABEL: define {{.*}} @test_complex_float_update()
// OGCG:         call void @__atomic_load(i64 noundef 8, ptr noundef @cfx,
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 8, ptr noundef @cfx,

// OGCG-LABEL: define {{.*}} @test_long_double_update()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @ldx,
// OGCG:         call {{.*}} @__atomic_compare_exchange(i64 noundef 16, ptr noundef @ldx,
