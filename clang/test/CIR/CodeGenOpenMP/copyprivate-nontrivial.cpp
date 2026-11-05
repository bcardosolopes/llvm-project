// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test copyprivate with non-trivially-copyable types calls the copy
// assignment operator instead of using memcpy.

struct S {
  int *p;
  S() : p(new int(0)) {}
  S(const S& o) : p(new int(*o.p)) {}
  S& operator=(const S& o) { *p = *o.p; return *this; }
  ~S() { delete p; }
};

void work(S &s);

void test_copyprivate_nontrivial() {
  S s;
  #pragma omp single copyprivate(s)
  {
    work(s);
  }
}
// The copyprivate copy function must call the copy assignment operator
// (_ZN1SaSERKS_) rather than using memcpy.
// CHECK: define {{.*}} @__omp_copyprivate_s_{{.*}}(ptr noundef %[[DST:.*]], ptr noundef %[[SRC:.*]])
// CHECK:   call {{.*}} ptr @_ZN1SaSERKS_(ptr {{.*}} %[[DST]], ptr {{.*}} %[[SRC]])
// CHECK:   ret void

// Also verify the copy function is referenced by __kmpc_copyprivate.
// CHECK: call void @__kmpc_copyprivate({{.*}} ptr @__omp_copyprivate_s_

// OGCG: define {{.*}} @_Z27test_copyprivate_nontrivialv()
// OGCG:   call i32 @__kmpc_global_thread_num(
// OGCG:   call i32 @__kmpc_single(
// OGCG:   call void @__kmpc_end_single(
// OGCG:   call void @__kmpc_copyprivate({{.*}} ptr @.omp.copyprivate.copy_func
// OGCG: define internal void @.omp.copyprivate.copy_func(
// OGCG:   call {{.*}} ptr @_ZN1SaSERKS_(
