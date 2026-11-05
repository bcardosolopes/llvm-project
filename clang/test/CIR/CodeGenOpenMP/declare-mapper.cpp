// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -femit-all-decls -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=EMITALL
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

class C {
public:
  int a;
  double *b;
};

#pragma omp declare mapper(id : C s) map(s.a, s.b[0:2])

// CIR: omp.declare_mapper @omp_mapper._ZTS1C.id : !rec_C {
// CIR: %[[FIELD_A:.*]] = omp.map.info
// CIR-SAME: var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i)
// CIR-SAME: map_clauses(tofrom)
// CIR-SAME: {name = "s.a"}
// CIR: %[[FIELD_B:.*]] = omp.map.info
// CIR-SAME: var_ptr(%{{.*}} : !cir.ptr<!cir.double>, !cir.double)
// CIR-SAME: map_clauses(tofrom)
// CIR-SAME: var_ptr_ptr(%{{.*}} : !cir.ptr<!cir.ptr<!cir.double>>)
// CIR-SAME: bounds(%{{[^)]+}})
// CIR-SAME: {name = "s.b[0:2]"}
// CIR: %[[PARENT:.*]] = omp.map.info
// CIR-SAME: var_ptr(%{{.*}} : !cir.ptr<!rec_C>, !rec_C)
// CIR-SAME: map_clauses(implicit)
// CIR-SAME: members(%[[FIELD_A]], %[[FIELD_B]] : [0], [1] : !cir.ptr<!s32i>, !cir.ptr<!cir.double>)
// CIR-SAME: {name = "s", partial_map = true}
// CIR: omp.declare_mapper.info map_entries(%[[PARENT]], %[[FIELD_A]], %[[FIELD_B]] : !cir.ptr<!rec_C>, !cir.ptr<!s32i>, !cir.ptr<!cir.double>)

// EMITALL: omp.declare_mapper @omp_mapper._ZTS1C.id : !rec_C {

void target_mapper(C c) {
  // CIR-LABEL: cir.func{{.*}} @_Z13target_mapper1C(
  // CIR: %[[MAP:.*]] = omp.map.info
  // CIR-SAME: map_clauses(tofrom)
  // CIR-SAME: mapper(@omp_mapper._ZTS1C.id)
  // CIR-SAME: {name = "c"}
  // CIR: omp.target map_entries(%[[MAP]]
  // LLVM-LABEL: define dso_local void @_Z13target_mapper1C(
  // LLVM: store ptr @.omp_mapper.omp_mapper._ZTS1C.id, ptr %{{.*}}, align 8
  // LLVM: call i32 @__tgt_target_kernel(
#pragma omp target map(mapper(id), tofrom : c)
  ++c.a;
}

void target_update_mapper(C c) {
  // CIR-LABEL: cir.func{{.*}} @_Z20target_update_mapper1C(
  // CIR: %[[TO:.*]] = omp.map.info
  // CIR-SAME: map_clauses(to)
  // CIR-SAME: mapper(@omp_mapper._ZTS1C.id)
  // CIR-SAME: {name = "c"}
  // CIR: omp.target_update map_entries(%[[TO]]
  // LLVM-LABEL: define dso_local void @_Z20target_update_mapper1C(
  // LLVM: store ptr @.omp_mapper.omp_mapper._ZTS1C.id, ptr %{{.*}}, align 8
  // LLVM: call void @__tgt_target_data_update_mapper(
#pragma omp target update to(mapper(id) : c)
  ;
}

// LLVM: define internal void @.omp_mapper.omp_mapper._ZTS1C.id(
// LLVM: call void @__tgt_push_mapper_component(

// OGCG: define {{.*}} @_Z13target_mapper1C(
// OGCG:   store ptr @.omp_mapper._ZTS1C.id, ptr %{{.*}}, align 8
// OGCG:   call i32 @__tgt_target_kernel(
// OGCG: define internal void @{{.*}}__omp_offloading_{{.*}}_l{{.*}}(
// OGCG: define internal void @.omp_mapper._ZTS1C.id(
// OGCG:   call void @__tgt_push_mapper_component(
// OGCG:   call i64 @__tgt_mapper_num_components(
// OGCG: define {{.*}} @_Z20target_update_mapper1C(
// OGCG:   store ptr @.omp_mapper._ZTS1C.id, ptr %{{.*}}, align 8
// OGCG:   call void @__tgt_target_data_update_mapper(
