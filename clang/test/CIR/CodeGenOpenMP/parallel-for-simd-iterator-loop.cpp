// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

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

void parallel_for_simd_iterator_loop(IterDouble ia, IterDouble ib,
                                     IterDouble ic) {
#pragma omp parallel for simd
  for (IterDouble i = ia; i < ib; ++i)
    *i = *ic * 0.5;
}

// CIR-LABEL: cir.func{{.*}} @_Z31parallel_for_simd_iterator_loop10IterDoubleS_S_(
// CIR: omp.parallel
// CIR: cir.call @_ZNK10IterDoublemiERKS_
// CIR: omp.wsloop nowait
// CIR: omp.simd
// CIR: omp.loop_nest
// CIR: cir.call @_ZN10IterDoubleplEi

// OGCG-LABEL: define dso_local void @_Z31parallel_for_simd_iterator_loop10IterDoubleS_S_(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG-LABEL: define internal void @_Z31parallel_for_simd_iterator_loop10IterDoubleS_S_.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
