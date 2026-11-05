// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -emit-llvm %s -o - | FileCheck %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -fno-clangir-direct-lowering -emit-llvm %s -o - | FileCheck %s %}

int cmp_eq(int *x, int e, int d) {
#pragma omp atomic compare
  if (*x == e)
    *x = d;
  return *x;
}

int cmp_eq_fail(int *x, int e, int d) {
#pragma omp atomic compare acq_rel fail(acquire)
  if (*x == e)
    *x = d;
  return *x;
}

double cmp_deq_fail(double *x, double e, double d) {
#pragma omp atomic compare fail(seq_cst)
  if (*x == e)
    *x = d;
  return *x;
}

unsigned cmp_umax(unsigned *x, unsigned e) {
#pragma omp atomic compare relaxed
  *x = *x < e ? e : *x;
  return *x;
}

double cmp_fmax_rev(double *x, double e) {
#pragma omp atomic compare release
  *x = e > *x ? e : *x;
  return *x;
}

float cmp_fmin(float *x, float e) {
#pragma omp atomic compare seq_cst
  if (*x > e)
    *x = e;
  return *x;
}

// CHECK-LABEL: define{{.*}} @_Z6cmp_eqPiii(
// CHECK: cmpxchg ptr {{.*}}, i32 {{.*}}, i32 {{.*}} monotonic monotonic, align 4
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z11cmp_eq_failPiii(
// CHECK: cmpxchg ptr {{.*}}, i32 {{.*}}, i32 {{.*}} acq_rel acquire, align 4
// CHECK: call void {{.*}}@__kmpc_flush
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z12cmp_deq_failPddd(
// CHECK: bitcast double
// CHECK: bitcast double
// CHECK: cmpxchg ptr {{.*}} monotonic seq_cst, align 8
// CHECK-NOT: @__kmpc_flush
// CHECK: ret double
//
// CHECK-LABEL: define{{.*}} @_Z8cmp_umaxPjj(
// CHECK: atomicrmw umax ptr {{.*}}, i32 {{.*}} monotonic, align 4
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z12cmp_fmax_revPdd(
// CHECK: atomicrmw fmax ptr {{.*}}, double {{.*}} release, align 8
// CHECK: call void {{.*}}@__kmpc_flush
// CHECK: ret double
//
// CHECK-LABEL: define{{.*}} @_Z8cmp_fminPff(
// CHECK: atomicrmw fmin ptr {{.*}}, float {{.*}} seq_cst, align 4
// CHECK: call void {{.*}}@__kmpc_flush
// CHECK: ret float
