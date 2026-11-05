// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void scalar_default() {
  int a = 1;
#pragma omp target map(a)
  a += 1;
}

void ptr_section_default(int *p) {
#pragma omp target map(p[0:4])
  p[0] += 1;
}

struct S {
  int a;
};

void obj_default(S *s) {
#pragma omp target map(s[0])
  s->a += 1;
}

struct ThisMap {
  int a;

  void obj_this_default() {
#pragma omp target map(this[0])
    a += 1;
  }
};

void use_this_default(ThisMap *t) {
  t->obj_this_default();
}

// CIR-LABEL: @_Z14scalar_defaultv(
// CIR: %[[SCALAR_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) -> !cir.ptr<!s32i> {name = "a"}

// CIR-LABEL: @_Z19ptr_section_defaultPi(
// CIR: %[[PTR_BOUNDS:.*]] = omp.map.bounds
// CIR: %[[PTR_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%[[PTR_BOUNDS]]) -> !cir.ptr<!s32i> {name = "p[0:4]"}

// CIR-LABEL: @_Z11obj_defaultP1S(
// CIR: %[[OBJ_BOUNDS:.*]] = omp.map.bounds
// CIR: %[[OBJ_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!rec_S>>) bounds(%[[OBJ_BOUNDS]]) -> !cir.ptr<!rec_S> {name = "s[0]"}

// CIR-LABEL: @_ZN7ThisMap16obj_this_defaultEv(
// CIR: %[[THIS_BOUNDS:.*]] = omp.map.bounds
// CIR: %[[THIS_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_ThisMap>, !rec_ThisMap) map_clauses(tofrom) capture(ByRef) bounds(%[[THIS_BOUNDS]]) -> !cir.ptr<!rec_ThisMap> {name = "this[0]"}

// LLVM-DAG: @.offload_sizes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 4]
// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 35]
// LLVM-DAG: @.offload_sizes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 16, i64 8]
// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 35, i64 16384]
// LLVM-DAG: @.offload_sizes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 4, i64 8]
// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [2 x i64] [i64 35, i64 16384]
// LLVM-DAG: @.offload_sizes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 4]
// LLVM-DAG: @.offload_maptypes{{(\.[0-9]+)?}} = private unnamed_addr constant [1 x i64] [i64 35]

// OGCG-LABEL: @_Z14scalar_defaultv(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z19ptr_section_defaultPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z11obj_defaultP1S(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_ZN7ThisMap16obj_this_defaultEv(
// OGCG: call i32 @__tgt_target_kernel(
