// RUN: %clang_cc1 -x c++ -std=c++11 -fopenmp -emit-mlir=cir-abi -fclangir -mmlir --mlir-print-op-generic %s -o - | FileCheck %s --check-prefix=ARRAY
// RUN: %clang_cc1 -x c++ -std=c++11 -fopenmp -emit-mlir=cir-abi -fclangir -mmlir --mlir-print-op-generic %s -o - | FileCheck %s --check-prefix=SCALAR
// RUN: %clang_cc1 -x c++ -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void add(short &out, short &in) { out += in; }

#pragma omp declare reduction(my_add : short : add(omp_out, omp_in))

// ARRAY: "omp.declare_reduction"() <{byref_element_type = !cir.array<!s16i x 1>, sym_name = "__omp_decl_reduction_my_add_byref{{.*}}", type = !cir.ptr<!cir.array<!s16i x 1>>}> ({
// ARRAY: ^bb0(%[[ARRAY_ORIG:arg[0-9]+]]: !cir.ptr<!cir.array<!s16i x 1>>{{.*}}, %[[ARRAY_PRIV:arg[0-9]+]]: !cir.ptr<!cir.array<!s16i x 1>>{{.*}}):
// ARRAY: %[[ARRAY_INIT_COUNT:.+]] = "cir.const"() <{value = #cir.int<1> : !u64i}> : () -> !u64i
// ARRAY: %[[ARRAY_INIT_ORIG_BEGIN:.+]] = "cir.cast"(%[[ARRAY_ORIG]]) {{.*}} : (!cir.ptr<!cir.array<!s16i x 1>>) -> !cir.ptr<!s16i>
// ARRAY: %[[ARRAY_INIT_PRIV_BEGIN:.+]] = "cir.cast"(%[[ARRAY_PRIV]]) {{.*}} : (!cir.ptr<!cir.array<!s16i x 1>>) -> !cir.ptr<!s16i>
// ARRAY: "cir.do"()
// ARRAY: %[[ARRAY_INIT_CUR_ORIG:.+]] = "cir.load"(%{{.+}}) <{alignment = 8 : i64}> : (!cir.ptr<!cir.ptr<!s16i>>) -> !cir.ptr<!s16i>
// ARRAY: %[[ARRAY_INIT_CUR_PRIV:.+]] = "cir.load"(%{{.+}}) <{alignment = 8 : i64}> : (!cir.ptr<!cir.ptr<!s16i>>) -> !cir.ptr<!s16i>
// ARRAY: %[[ARRAY_INIT_ZERO:.+]] = "cir.const"() <{value = #cir.int<0> : !s16i}> : () -> !s16i
// ARRAY: "cir.store"(%[[ARRAY_INIT_ZERO]], %[[ARRAY_INIT_CUR_PRIV]]) <{alignment = 2 : i64}> : (!s16i, !cir.ptr<!s16i>) -> ()
// ARRAY: ^bb0(%[[ARRAY_LHS:arg[0-9]+]]: !cir.ptr<!cir.array<!s16i x 1>>{{.*}}, %[[ARRAY_RHS:arg[0-9]+]]: !cir.ptr<!cir.array<!s16i x 1>>{{.*}}):
// ARRAY: "cir.do"()
// ARRAY: "cir.call"(%{{.+}}, %{{.+}}) <{callee = @_Z3addRsS_
// ARRAY: "omp.wsloop"(%{{.+}}) <{{.*}}reduction_byref = array<i1: true>, reduction_syms = [@__omp_decl_reduction_my_add_byref{{.*}}]}>

void array_udr() {
  short y[1];
#pragma omp parallel for reduction(my_add : y)
  for (int i = 0; i < 1; ++i) {
  }
}

template <class T>
struct Box {
#pragma omp declare reduction(fun : T : omp_out ^= omp_in) initializer(omp_priv = 24 + omp_orig)
};

// SCALAR: "omp.declare_reduction"() <{sym_name = "__omp_decl_reduction_fun_s32{{.*}}", type = !s32i}> ({
// SCALAR: ^bb0(%[[SCALAR_INIT_ARG:arg[0-9]+]]: !s32i{{.*}}):
// SCALAR: %[[SCALAR_INIT_ORIG_SLOT:.+]] = "cir.alloca"() <{alignment = 4 : i64, allocaType = !s32i, name = "__omp_udr_orig"}> : () -> !cir.ptr<!s32i>
// SCALAR: %[[SCALAR_INIT_PRIV_SLOT:.+]] = "cir.alloca"() <{alignment = 4 : i64, allocaType = !s32i, name = "__omp_udr_priv"}> : () -> !cir.ptr<!s32i>
// SCALAR: "cir.store"(%[[SCALAR_INIT_ARG]], %[[SCALAR_INIT_ORIG_SLOT]]) <{alignment = 4 : i64}> : (!s32i, !cir.ptr<!s32i>) -> ()
// SCALAR: %[[SCALAR_INIT_PRIV:.+]] = "cir.load"(%[[SCALAR_INIT_PRIV_SLOT]]) <{alignment = 4 : i64}> : (!cir.ptr<!s32i>) -> !s32i
// SCALAR: "omp.yield"(%[[SCALAR_INIT_PRIV]]) : (!s32i) -> ()
// SCALAR: ^bb0(%[[SCALAR_LHS_ARG:arg[0-9]+]]: !s32i{{.*}}, %[[SCALAR_RHS_ARG:arg[0-9]+]]: !s32i{{.*}}):
// SCALAR: %[[SCALAR_LHS_SLOT:.+]] = "cir.alloca"() <{alignment = 4 : i64, allocaType = !s32i, name = "__omp_udr_lhs"}> : () -> !cir.ptr<!s32i>
// SCALAR: %[[SCALAR_RHS_SLOT:.+]] = "cir.alloca"() <{alignment = 4 : i64, allocaType = !s32i, name = "__omp_udr_rhs"}> : () -> !cir.ptr<!s32i>
// SCALAR: "cir.store"(%[[SCALAR_LHS_ARG]], %[[SCALAR_LHS_SLOT]]) <{alignment = 4 : i64}> : (!s32i, !cir.ptr<!s32i>) -> ()
// SCALAR: "cir.store"(%[[SCALAR_RHS_ARG]], %[[SCALAR_RHS_SLOT]]) <{alignment = 4 : i64}> : (!s32i, !cir.ptr<!s32i>) -> ()
// SCALAR: "omp.parallel"(%{{.+}}) <{{.*}}reduction_byref = array<i1: false>, reduction_syms = [@__omp_decl_reduction_fun_s32{{.*}}]}>

void scalar_udr() {
  int i = 0;
#pragma omp parallel reduction(Box<int>::fun : i)
  {
  }
}

// OGCG: define {{.*}} @_Z9array_udrv()
// OGCG:   call void {{.*}} @__kmpc_fork_call({{.*}} @_Z9array_udrv.omp_outlined
// OGCG: define internal void @_Z9array_udrv.omp_outlined(
// OGCG:   call i32 @__kmpc_reduce_nowait(
// OGCG:   call void @__kmpc_end_reduce_nowait(
// OGCG: define internal void @.omp_combiner.(
// OGCG:   call {{.*}} @_Z3addRsS_(
// OGCG: define {{.*}} @_Z10scalar_udrv()
// OGCG:   call void {{.*}} @__kmpc_fork_call({{.*}} @_Z10scalar_udrv.omp_outlined
// OGCG: define internal void @_Z10scalar_udrv.omp_outlined(
// OGCG:   call i32 @__kmpc_reduce_nowait(
// OGCG:   call void @__kmpc_end_reduce_nowait(
// OGCG: define internal void @.omp_combiner.{{.*}}(
// OGCG: define internal void @.omp_initializer.(
