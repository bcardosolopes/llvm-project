// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++17 -fopenmp -I %S/../../Headers/Inputs/include -emit-mlir=cir-abi -fclangir -mmlir --mlir-print-op-generic %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++17 -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm -disable-llvm-passes -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++17 -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

#include <omp.h>

struct S {
  int a;
  S() : a(0) {}
  S(const S &) {}
  S &operator=(const S &) { return *this; }
  ~S() {}
  friend S operator+(const S &x, const S &y) { return x; }
};

// CIR: "omp.declare_reduction"() <{byref_element_type = !cir.array<!rec_S x 5>, sym_name = "__omp_reduction_add_byref_{{.*}}", type = !cir.ptr<!cir.array<!rec_S x 5>>}> ({
// CIR: %[[ALLOC:.*]] = "cir.alloca"() <{alignment = 16 : i64, allocaType = !cir.array<!rec_S x 5>, name = "c"}> : () -> !cir.ptr<!cir.array<!rec_S x 5>>
// CIR: ^bb0(%{{.*}}: !cir.ptr<!cir.array<!rec_S x 5>>{{.*}}, %[[INITARG:arg[0-9]+]]: !cir.ptr<!cir.array<!rec_S x 5>>{{.*}}):
// CIR: %[[INITCOUNT:.*]] = "cir.const"() <{value = #cir.int<5> : !u64i}> : () -> !u64i
// CIR: %[[INITBEGIN:.*]] = "cir.cast"(%[[INITARG]]) {{.*}} : (!cir.ptr<!cir.array<!rec_S x 5>>) -> !cir.ptr<!rec_S>
// CIR: "cir.do"()
// CIR: %[[INITELEM:.*]] = "cir.load"(%{{.*}}) <{alignment = 8 : i64}> : (!cir.ptr<!cir.ptr<!rec_S>>) -> !cir.ptr<!rec_S>
// CIR: "cir.call"(%[[INITELEM]]) <{callee = @_ZN1SC1Ev
// CIR: ^bb0(%[[LHSARG:arg[0-9]+]]: !cir.ptr<!cir.array<!rec_S x 5>>{{.*}}, %[[RHSARG:arg[0-9]+]]: !cir.ptr<!cir.array<!rec_S x 5>>{{.*}}):
// CIR: %[[REFTMP:.*]] = "cir.alloca"() <{alignment = 4 : i64, allocaType = !rec_S, name = "ref.tmp0"}> : () -> !cir.ptr<!rec_S>
// CIR: "cir.call"(%[[REFTMP]], %{{.*}}, %{{.*}}) <{callee = @_ZplRK1SS1_
// CIR: "cir.call"(%{{.*}}, %[[REFTMP]]) <{callee = @_ZN1SaSERKS_
// CIR: "cir.call"(%[[REFTMP]]) <{callee = @_ZN1SD1Ev
// CIR: ^bb0(%[[CLEANARG:arg[0-9]+]]: !cir.ptr<!cir.array<!rec_S x 5>>{{.*}}):
// CIR: %[[CLEANCOUNT:.*]] = "cir.const"() <{value = #cir.int<5> : !u64i}> : () -> !u64i
// CIR: "cir.do"()
// CIR: %[[CURPTR:.*]] = "cir.load"(%{{.*}}) : (!cir.ptr<!cir.ptr<!rec_S>>) -> !cir.ptr<!rec_S>
// CIR: %[[PREV:.*]] = "cir.ptr_stride"(%[[CURPTR]], %{{.*}}) <{noWrapFlags = 3 : i32}> : (!cir.ptr<!rec_S>, !s64i) -> !cir.ptr<!rec_S>
// CIR: "cir.call"(%[[PREV]]) <{callee = @_ZN1SD1Ev
// LLVM-LABEL: define linkonce_odr void @_ZplRK1SS1_(ptr dead_on_unwind noalias writable sret(%struct.S) align 4 %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})
// LLVM-LABEL: define internal void @.omp.task.reduction.comb(ptr %{{.*}}, ptr %{{.*}})
// LLVM-DAG: %[[SRET_TMP:.*]] = alloca %struct.S, i64 1, align 4
// LLVM-DAG: call void @_ZplRK1SS1_(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[SRET_TMP]], ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})
// LLVM-DAG: call noundef nonnull align 4 dereferenceable(4) ptr @_ZN1SaSERKS_(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %[[SRET_TMP]])
// LLVM-DAG: call void @_ZN1SD1Ev(ptr noundef nonnull align 4 dereferenceable(4) %[[SRET_TMP]]) #{{[0-9]+}}

// OGCG-LABEL: define internal void @.red_comb.(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-DAG: %{{.*}} = alloca %struct.S, align 4
// OGCG-DAG: call void @_ZplRK1SS1_(ptr dead_on_unwind writable sret(%struct.S) align 4 %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})
// OGCG-DAG: call noundef nonnull align 4 dereferenceable(4) ptr @_ZN1SaSERKS_(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})
// OGCG-DAG: call void @_ZN1SD1Ev(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}) #{{[0-9]+}}
// OGCG-LABEL: define linkonce_odr void @_ZplRK1SS1_(ptr dead_on_unwind noalias writable sret(%struct.S) align 4 %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})

void taskgroup_nontrivial_array_reduction() {
  S c[5];
  // CIR-LABEL: "cir.func"() <{{.*}}sym_name = "_Z{{[0-9]+}}taskgroup_nontrivial_array_reductionv"
#pragma omp taskgroup task_reduction(+: c)
  {
// CIR: "omp.taskgroup"(%{{.*}}) <{operandSegmentSizes = array<i32: 0, 0, 1>, task_reduction_byref = array<i1: true>, task_reduction_syms = [@__omp_reduction_add_byref_{{.*}}]}>
  }
}
