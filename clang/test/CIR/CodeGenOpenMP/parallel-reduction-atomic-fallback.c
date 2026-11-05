// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Scalar reduction types without a native LLVM `atomicrmw` (`_Bool`,
// `long double` (x86 fp80), `_Complex`) must NOT emit an `cir.atomic.fetch`
// atomic-combiner region. Doing so previously produced malformed IR for
// `_Bool` (a verifier error) and an unsupported-type error for `long double` /
// `_Complex`. Omitting the atomic region makes the OpenMP runtime fall back to
// a critical-section combine, matching classic CodeGen. Types that do have a
// native `atomicrmw` (integers, `float`, `double`) keep the atomic region.

int test_int_reduction(int *a, int n) {
  int s = 0;
#pragma omp parallel for reduction(+: s)
  for (int i = 0; i < n; i++)
    s += a[i];
  return s;
}

_Bool test_bool_reduction(_Bool *a, int n) {
  _Bool s = 0;
#pragma omp parallel for reduction(||: s)
  for (int i = 0; i < n; i++)
    s = s || a[i];
  return s;
}

long double test_long_double_reduction(long double *a, int n) {
  long double s = 0;
#pragma omp parallel for reduction(+: s)
  for (int i = 0; i < n; i++)
    s += a[i];
  return s;
}

double _Complex test_complex_reduction(double _Complex *a, int n) {
  double _Complex s = 0;
#pragma omp parallel for reduction(+: s)
  for (int i = 0; i < n; i++)
    s += a[i];
  return s;
}

// The declare_reduction ops are emitted at module scope in first-use order:
// complex, long double, bool, then int. Only the int reduction carries an
// atomic-combiner region.

// CIR: omp.declare_reduction @__omp_reduction_add_byref__cir_complex_cir_double{{.*}}byref_element_type = !cir.complex<!cir.double>
// CIR-NOT: cir.atomic.fetch
// CIR: omp.declare_reduction @__omp_reduction_add_f80 : !cir.long_double<!cir.f80>
// CIR-NOT: cir.atomic.fetch
// CIR: omp.declare_reduction @__omp_reduction_lor_bool : !cir.bool
// CIR-NOT: cir.atomic.fetch
// CIR: omp.declare_reduction @__omp_reduction_add_s32 : !s32i
// CIR: atomic {
// CIR: cir.atomic.fetch add relaxed fetch_first

// LLVM-LABEL: define dso_local i32 @test_int_reduction(
// LLVM-LABEL: define {{.*}} @test_bool_reduction(
// LLVM-LABEL: define {{.*}} @test_long_double_reduction(
// LLVM-LABEL: define {{.*}} @test_complex_reduction(
