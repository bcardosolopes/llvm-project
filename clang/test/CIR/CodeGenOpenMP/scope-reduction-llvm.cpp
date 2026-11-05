// RUN: %clang_cc1 -fopenmp -fopenmp-version=52 -triple x86_64-unknown-linux-gnu -x c++ -std=c++11 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=52 -triple x86_64-unknown-linux-gnu -x c++ -std=c++11 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int local_scope(int x) {
  int s = 0;
#pragma omp parallel firstprivate(x)
  {
#pragma omp scope reduction(+:s)
    s += x;
#pragma omp scope nowait reduction(+:s)
    s += x;
  }
  return s;
}

struct S {
  int e;

  void member_scope() {
#pragma omp parallel
    {
#pragma omp scope reduction(+:e)
      e += 1;
    }
  }
};

void call_member_scope(S &s) {
  s.member_scope();
}

// CHECK-LABEL: define{{.*}} @_Z11local_scopei
// CHECK: call void {{.*}}@__kmpc_fork_call(
//
// CHECK-LABEL: define internal void @_Z11local_scopei..omp_par
// CHECK: call i32 @__kmpc_reduce(
// CHECK: atomicrmw add ptr
// CHECK: call void @__kmpc_barrier(
// CHECK: call i32 @__kmpc_reduce_nowait(
// CHECK: atomicrmw add ptr
//
// CHECK-LABEL: define{{.*}} @_ZN1S12member_scopeEv
// CHECK: call void {{.*}}@__kmpc_fork_call(
//
// CHECK-LABEL: define internal void @_ZN1S12member_scopeEv..omp_par
// CHECK: call i32 @__kmpc_reduce(
//
// CHECK-LABEL: define{{.*}} @_Z17call_member_scopeR1S
// CHECK: call void @_ZN1S12member_scopeEv

// OGCG-LABEL: define{{.*}} @_Z11local_scopei(
// OGCG:   call void {{.*}}@__kmpc_fork_call(
// OGCG: define internal void @_Z11local_scopei.omp_outlined(
// OGCG:   call i32 @__kmpc_reduce(
// OGCG:   atomicrmw add ptr
// OGCG:   call i32 @__kmpc_reduce_nowait(
// OGCG:   atomicrmw add ptr
// OGCG: define{{.*}} @_ZN1S12member_scopeEv(
// OGCG:   call void {{.*}}@__kmpc_fork_call(
// OGCG: define internal void @_ZN1S12member_scopeEv.omp_outlined(
// OGCG:   call i32 @__kmpc_reduce(
