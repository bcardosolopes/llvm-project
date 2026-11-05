// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target

// Verify that firstprivate for aggregate types (both trivially-copyable and
// non-trivially-copyable) generates correct copy semantics.

struct POD { int x; double y; };

struct S {
  int *p;
  S() : p(new int(0)) {}
  S(const S& o) : p(new int(*o.p)) {}
  ~S() { delete p; }
};

void work_pod(const POD &p);
void work_s(const S &s);

// --- Trivially-copyable aggregate: firstprivate uses memcpy ---

void test_firstprivate_pod() {
  POD p = {1, 2.0};
  #pragma omp parallel for firstprivate(p)
  for (int i = 0; i < 10; i++)
    work_pod(p);
}

// CHECK-LABEL: define {{.*}} @_Z21test_firstprivate_podv..omp_par(
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr {{.*}} %omp.private.alloc, ptr {{.*}}, i64 16, i1 false)

// --- Non-trivially-copyable aggregate: firstprivate calls copy ctor ---

void test_firstprivate_nontrivial() {
  S s;
  #pragma omp parallel for firstprivate(s)
  for (int i = 0; i < 10; i++)
    work_s(s);
}

// CHECK-LABEL: define internal void @__omp_firstprivate_copy_s_{{[0-9]+}}_{{[0-9]+}}(
// CHECK:   call void @_ZN1SC1ERKS_(
//
// CHECK-LABEL: define {{.*}} @_Z28test_firstprivate_nontrivialv..omp_par(
// CHECK:   call void @__omp_firstprivate_copy_s_{{[0-9]+}}_{{[0-9]+}}(ptr noundef %omp.private.alloc, ptr noundef %loadgep_)

// --- Mixed scalar + aggregate firstprivate ---

void test_firstprivate_mixed() {
  S a;
  int x = 42;
  POD p = {1, 2.0};
  #pragma omp parallel for firstprivate(a, x, p)
  for (int i = 0; i < 10; i++)
    work_s(a);
}

// CHECK-LABEL: define internal void @__omp_firstprivate_copy_a_{{[0-9]+}}_{{[0-9]+}}(
// CHECK:   call void @_ZN1SC1ERKS_(
//
// CHECK-LABEL: define {{.*}} @_Z23test_firstprivate_mixedv..omp_par(
// CHECK-DAG:   call void @__omp_firstprivate_copy_a_{{[0-9]+}}_{{[0-9]+}}(
// CHECK-DAG:   call void @llvm.memcpy.p0.p0.i64(

// OGCG: define internal void @_Z21test_firstprivate_podv.omp_outlined(
// OGCG:   call void @llvm.memcpy.p0.p0.i64(
// OGCG: define internal void @_Z28test_firstprivate_nontrivialv.omp_outlined(
// OGCG:   call void @_ZN1SC1ERKS_(
// OGCG: define internal void @_Z23test_firstprivate_mixedv.omp_outlined(
// OGCG:   call void @_ZN1SC1ERKS_(
// OGCG:   call void @llvm.memcpy.p0.p0.i64(
