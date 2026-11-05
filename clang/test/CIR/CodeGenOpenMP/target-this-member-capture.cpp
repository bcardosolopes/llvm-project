// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

struct S {
  int *p;

  S(int *q) : p(q) {
#pragma omp target
    p[0] += 1;
  }

  void target_parallel() {
#pragma omp target parallel
    p[0] += 2;
  }

  void target_teams() {
#pragma omp target teams
    p[0] += 3;
  }
};

struct ExplicitThisMap {
  int a;

  void target_explicit_this_map() {
#pragma omp target map(this[0])
    a += 4;
  }
};

void use(int *q, S *s) {
  S local(q);
  s->target_parallel();
  s->target_teams();
}

void use_explicit_this_map(ExplicitThisMap *s) {
  s->target_explicit_this_map();
}

// CIR-LABEL: @_ZN1SC{{[12]}}EPi(
// CIR: %[[CTOR_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(implicit, tofrom) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "this->p"}
// CIR: %[[CTOR_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(implicit, tofrom) capture(ByRef) members(%[[CTOR_CHILD]] : [0] : !cir.ptr<!cir.ptr<!s32i>>) -> !cir.ptr<!rec_S> {name = "this", partial_map = true}
// CIR: omp.target map_entries(%[[CTOR_PARENT]] -> %[[CTOR_THIS:arg[0-9]+]], %[[CTOR_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!s32i>>) {
// CIR: %[[CTOR_P:.*]] = cir.get_member %[[CTOR_THIS]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_ZN1S15target_parallelEv(
// CIR: %[[PAR_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(implicit, tofrom) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "this->p"}
// CIR: %[[PAR_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(implicit, tofrom) capture(ByRef) members(%[[PAR_CHILD]] : [0] : !cir.ptr<!cir.ptr<!s32i>>) -> !cir.ptr<!rec_S> {name = "this", partial_map = true}
// CIR: omp.target map_entries(%[[PAR_PARENT]] -> %[[PAR_THIS:arg[0-9]+]], %[[PAR_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!s32i>>) {
// CIR: omp.parallel
// CIR: %[[PAR_P:.*]] = cir.get_member %[[PAR_THIS]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_ZN1S12target_teamsEv(
// CIR: %[[TEAM_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>) map_clauses(implicit, tofrom) capture(ByRef) -> !cir.ptr<!cir.ptr<!s32i>> {name = "this->p"}
// CIR: %[[TEAM_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(implicit, tofrom) capture(ByRef) members(%[[TEAM_CHILD]] : [0] : !cir.ptr<!cir.ptr<!s32i>>) -> !cir.ptr<!rec_S> {name = "this", partial_map = true}
// CIR: omp.target map_entries(%[[TEAM_PARENT]] -> %[[TEAM_THIS:arg[0-9]+]], %[[TEAM_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!s32i>>) {
// CIR: omp.teams
// CIR: %[[TEAM_P:.*]] = cir.get_member %[[TEAM_THIS]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_ZN15ExplicitThisMap24target_explicit_this_mapEv(
// CIR: %[[THIS_BOUNDS:.*]] = omp.map.bounds
// CIR: %[[THIS_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_ExplicitThisMap>, !rec_ExplicitThisMap) map_clauses(tofrom) capture(ByRef) bounds(%[[THIS_BOUNDS]]) -> !cir.ptr<!rec_ExplicitThisMap> {name = "this[0]"}
// CIR: omp.target map_entries(%[[THIS_MAP]] -> %[[THIS_ARG:arg[0-9]+]] : !cir.ptr<!rec_ExplicitThisMap>) {
// CIR: %[[THIS_FIELD:.*]] = cir.get_member %[[THIS_ARG]][0] {name = "a"} : !cir.ptr<!rec_ExplicitThisMap> -> !cir.ptr<!s32i>

// LLVM-DAG: @.offload_sizes.{{[0-9]+}} = private unnamed_addr constant [1 x i64] [i64 4]
// LLVM-DAG: @.offload_maptypes.{{[0-9]+}} = private unnamed_addr constant [1 x i64] [i64 35]

// OGCG-DAG: define linkonce_odr void @_ZN1S15target_parallelEv(
// OGCG-DAG: call i32 @__tgt_target_kernel(

// OGCG-DAG: define linkonce_odr void @_ZN1S12target_teamsEv(
// OGCG-DAG: call i32 @__tgt_target_kernel(

// OGCG-DAG: define linkonce_odr void @_ZN1SC2EPi(
// OGCG-DAG: call i32 @__tgt_target_kernel(

// OGCG-DAG: define linkonce_odr void @_ZN15ExplicitThisMap24target_explicit_this_mapEv(
// OGCG-DAG: call i32 @__tgt_target_kernel(
