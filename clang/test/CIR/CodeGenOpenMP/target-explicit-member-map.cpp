// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

struct S {
  int *p;
};

void target_local_map(int *q) {
  S s{q};
#pragma omp target map(tofrom: s.p[0:1])
  s.p[0] += 1;
}

void target_parallel_local_map(int *q) {
  S s{q};
#pragma omp target parallel map(tofrom: s.p[0:1])
  s.p[0] += 2;
}

void target_teams_local_map(int *q) {
  S s{q};
#pragma omp target teams map(tofrom: s.p[0:1])
  s.p[0] += 3;
}

void target_param_map(S s) {
#pragma omp target map(tofrom: s.p[0:1])
  s.p[0] += 4;
}

S g;

void target_global_map() {
#pragma omp target map(tofrom: g.p[0:1])
  g.p[0] += 5;
}

// CIR-LABEL: @_Z16target_local_mapPi(
// CIR: %[[LOCAL_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%{{.*}}) -> !cir.ptr<!s32i> {name = "s.p[0:1]"}
// CIR: %[[LOCAL_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(tofrom) capture(ByRef) members(%[[LOCAL_CHILD]] : [0] : !cir.ptr<!s32i>) -> !cir.ptr<!rec_S> {name = "s", partial_map = true}
// CIR: omp.target map_entries(%[[LOCAL_PARENT]] -> %[[LOCAL_S:arg[0-9]+]], %[[LOCAL_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!s32i>) {
// CIR: %[[LOCAL_P:.*]] = cir.get_member %[[LOCAL_S]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_Z25target_parallel_local_mapPi(
// CIR: %[[PAR_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%{{.*}}) -> !cir.ptr<!s32i> {name = "s.p[0:1]"}
// CIR: %[[PAR_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(tofrom) capture(ByRef) members(%[[PAR_CHILD]] : [0] : !cir.ptr<!s32i>) -> !cir.ptr<!rec_S> {name = "s", partial_map = true}
// CIR: omp.target map_entries(%[[PAR_PARENT]] -> %[[PAR_S:arg[0-9]+]], %[[PAR_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!s32i>) {
// CIR: omp.parallel
// CIR: %[[PAR_P:.*]] = cir.get_member %[[PAR_S]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_Z22target_teams_local_mapPi(
// CIR: %[[TEAM_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%{{.*}}) -> !cir.ptr<!s32i> {name = "s.p[0:1]"}
// CIR: %[[TEAM_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(tofrom) capture(ByRef) members(%[[TEAM_CHILD]] : [0] : !cir.ptr<!s32i>) -> !cir.ptr<!rec_S> {name = "s", partial_map = true}
// CIR: omp.target map_entries(%[[TEAM_PARENT]] -> %[[TEAM_S:arg[0-9]+]], %[[TEAM_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!s32i>) {
// CIR: omp.teams
// CIR: %[[TEAM_P:.*]] = cir.get_member %[[TEAM_S]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_Z16target_param_map1S(
// CIR: %[[PARAM_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%{{.*}}) -> !cir.ptr<!s32i> {name = "s.p[0:1]"}
// CIR: %[[PARAM_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(tofrom) capture(ByRef) members(%[[PARAM_CHILD]] : [0] : !cir.ptr<!s32i>) -> !cir.ptr<!rec_S> {name = "s", partial_map = true}
// CIR: omp.target map_entries(%[[PARAM_PARENT]] -> %[[PARAM_S:arg[0-9]+]], %[[PARAM_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!s32i>) {
// CIR: %[[PARAM_P:.*]] = cir.get_member %[[PARAM_S]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// CIR-LABEL: @_Z17target_global_mapv(
// CIR: %[[GLOBAL_CHILD:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!s32i>>) bounds(%{{.*}}) -> !cir.ptr<!s32i> {name = "g.p[0:1]"}
// CIR: %[[GLOBAL_PARENT:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!rec_S>, !rec_S) map_clauses(tofrom) capture(ByRef) members(%[[GLOBAL_CHILD]] : [0] : !cir.ptr<!s32i>) -> !cir.ptr<!rec_S> {name = "g", partial_map = true}
// CIR: omp.target map_entries(%[[GLOBAL_PARENT]] -> %[[GLOBAL_S:arg[0-9]+]], %[[GLOBAL_CHILD]] -> %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!s32i>) {
// CIR: %[[GLOBAL_P:.*]] = cir.get_member %[[GLOBAL_S]][0] {name = "p"} : !cir.ptr<!rec_S> -> !cir.ptr<!cir.ptr<!s32i>>

// OGCG-LABEL: @_Z16target_local_mapPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z25target_parallel_local_mapPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z22target_teams_local_mapPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z16target_param_map1S(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z17target_global_mapv(
// OGCG: call i32 @__tgt_target_kernel(
