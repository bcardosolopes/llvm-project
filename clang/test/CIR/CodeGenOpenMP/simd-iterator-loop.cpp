// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

typedef int MyIdx;

class IterDouble {
  double *Ptr;

public:
  IterDouble operator++() const {
    IterDouble next;
    next.Ptr = Ptr + 1;
    return next;
  }

  bool operator<(const IterDouble &that) const { return Ptr < that.Ptr; }
  double &operator*() const { return *Ptr; }
  MyIdx operator-(const IterDouble &that) const {
    return (MyIdx)(Ptr - that.Ptr);
  }

  IterDouble operator+(int delta) {
    IterDouble next;
    next.Ptr = Ptr + delta;
    return next;
  }
};

void simd_iterator_loop(IterDouble ia, IterDouble ib, IterDouble ic) {
#pragma omp simd
  for (IterDouble i = ia; i < ib; ++i)
    *i = *ic * 0.5;
}

// CIR-LABEL: cir.func{{.*}} @_Z18simd_iterator_loop10IterDoubleS_S_(
// CIR: cir.call @_ZNK10IterDoublemiERKS_
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.call @_ZN10IterDoubleplEi

// OGCG-LABEL: define {{.*}} @_Z18simd_iterator_loop10IterDoubleS_S_(
// OGCG: br label %omp.inner.for.cond
