// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test lastprivate with non-trivially-copyable types calls the copy
// assignment operator instead of using memcpy.

struct S {
  int *p;
  S() : p(new int(0)) {}
  S(const S& o) : p(new int(*o.p)) {}
  S& operator=(const S& o) { *p = *o.p; return *this; }
  ~S() { delete p; }
};

void work(S &s);

void test_lastprivate_nontrivial() {
  S s;
  #pragma omp parallel for lastprivate(s)
  for (int i = 0; i < 10; i++)
    work(s);
}

// The lastprivate copy-back on the last iteration must call the copy
// assignment operator (_ZN1SaSERKS_) rather than using memcpy.
// CHECK-LABEL: define {{.*}} @_Z27test_lastprivate_nontrivialv..omp_par(
// CHECK:   call {{.*}} ptr @_ZN1SaSERKS_(ptr {{.*}}, ptr {{.*}})

// OGCG: define internal void @_Z27test_lastprivate_nontrivialv.omp_outlined(
// OGCG:   call {{.*}} ptr @_ZN1SaSERKS_(
