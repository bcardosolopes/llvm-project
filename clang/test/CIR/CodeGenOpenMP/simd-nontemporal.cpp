// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void simd_scalars() {
  double a = 1.0;
  double b = 2.0;
#pragma omp simd nontemporal(a, b)
  for (int i = 0; i < 4; ++i)
    a = b + i;
}

struct Base {
  float a;
};

struct T : Base {
  float other;

  void member() {
#pragma omp simd nontemporal(Base::a)
    for (int i = 0; i < 4; ++i) {
      Base::a = 0.0f;
      other = 1.0f;
    }
  }
};

void call_member(T *t) { t->member(); }

// CIR-LABEL: cir.func{{.*}} @_Z12simd_scalarsv(
// CIR: omp.simd nontemporal(
// CIR: cir.load nontemporal
// CIR: cir.store nontemporal

// CIR-LABEL: cir.func{{.*}} @_ZN1T6memberEv(
// CIR: omp.simd nontemporal(
// CIR: cir.store nontemporal
// CIR-NOT: cir.store nontemporal
// CIR: cir.store

// LLVM-LABEL: define{{.*}} @_Z12simd_scalarsv(
// LLVM: load double, ptr %{{.*}}, align 8, !nontemporal ![[NT0:[0-9]+]]
// LLVM: store double %{{.*}}, ptr %{{.*}}, align 8, !nontemporal ![[NT0]]

// LLVM-LABEL: define{{.*}} @_ZN1T6memberEv(
// LLVM: store float 0.000000e+00, ptr %{{.*}}, align 4, !nontemporal ![[NT1:[0-9]+]]
// LLVM-NOT: store float {{.*}}!nontemporal
// LLVM: store float 1.000000e+00, ptr %{{.*}}, align 4

// OGCG-LABEL: define {{.*}} @_Z12simd_scalarsv(
// OGCG: br label %omp.inner.for.cond

// OGCG-LABEL: define {{.*}} @_ZN1T6memberEv(
// OGCG: br label %omp.inner.for.cond
