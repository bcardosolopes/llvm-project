// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -emit-llvm %s -o - | FileCheck %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=51 -fclangir -fno-clangir-direct-lowering -emit-llvm %s -o - | FileCheck %s %}

int cmp_eq_old(int *x, int *v, int e, int d) {
#pragma omp atomic compare capture
  {
    *v = *x;
    if (*x == e)
      *x = d;
  }
  return *v;
}

int cmp_eq_new(int *x, int *v, int e, int d) {
#pragma omp atomic compare capture
  {
    if (*x == e)
      *x = d;
    *v = *x;
  }
  return *v;
}

int cmp_eq_result(int *x, int *r, int e, int d) {
#pragma omp atomic compare capture
  {
    *r = *x == e;
    if (*r)
      *x = d;
  }
  return *r;
}

int cmp_eq_fail_only(int *x, int *v, int e, int d) {
#pragma omp atomic compare capture acq_rel fail(acquire)
  if (*x == e) {
    *x = d;
  } else {
    *v = *x;
  }
  return *v;
}

int cmp_min_new(int *x, int *v, int e) {
#pragma omp atomic compare capture release
  {
    if (*x > e)
      *x = e;
    *v = *x;
  }
  return *v;
}

// CHECK-LABEL: define{{.*}} @_Z10cmp_eq_oldPiS_ii(
// CHECK: cmpxchg ptr {{.*}}, i32 {{.*}}, i32 {{.*}} monotonic monotonic, align 4
// CHECK: extractvalue { i32, i1 } {{.*}}, 0
// CHECK: store i32 {{.*}}, ptr {{.*}}, align 4
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z10cmp_eq_newPiS_ii(
// CHECK: cmpxchg ptr {{.*}}, i32 {{.*}}, i32 {{.*}} monotonic monotonic, align 4
// CHECK: extractvalue { i32, i1 } {{.*}}, 0
// CHECK: extractvalue { i32, i1 } {{.*}}, 1
// CHECK: select i1 {{.*}}, i32 {{.*}}, i32 {{.*}}
// CHECK: store i32 {{.*}}, ptr {{.*}}, align 4
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z13cmp_eq_resultPiS_ii(
// CHECK: cmpxchg ptr {{.*}}, i32 {{.*}}, i32 {{.*}} monotonic monotonic, align 4
// CHECK: extractvalue { i32, i1 } {{.*}}, 1
// CHECK: store i32 {{.*}}, ptr {{.*}}, align 4
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z16cmp_eq_fail_onlyPiS_ii(
// CHECK: cmpxchg ptr {{.*}}, i32 {{.*}}, i32 {{.*}} acq_rel acquire, align 4
// CHECK: extractvalue { i32, i1 } {{.*}}, 0
// CHECK: extractvalue { i32, i1 } {{.*}}, 1
// CHECK: br i1 {{.*}}, label %{{.*}}, label %{{.*}}
// CHECK: store i32 {{.*}}, ptr {{.*}}, align 4
// CHECK: call void {{.*}}@__kmpc_flush
// CHECK: ret i32
//
// CHECK-LABEL: define{{.*}} @_Z11cmp_min_newPiS_i(
// CHECK: atomicrmw min ptr {{.*}}, i32 {{.*}} release, align 4
// CHECK: icmp slt i32 {{.*}}, {{.*}}
// CHECK: select i1 {{.*}}, i32 {{.*}}, i32 {{.*}}
// CHECK: store i32 {{.*}}, ptr {{.*}}, align 4
// CHECK: call void {{.*}}@__kmpc_flush
// CHECK: ret i32
