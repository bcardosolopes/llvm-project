// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void simd_lastprivate_conditional() {
  int A = -1;
#pragma omp simd lastprivate(conditional: A)
  for (long long i = -10; i < 10; i += 3)
    A += (int)i;
}

struct S {
  int x;
  void f();
};

void S::f() {
#pragma omp simd lastprivate(conditional: x)
  for (long long i = 0; i < 4; ++i)
    x = (int)i;
}

// CIR:       omp.critical.declare @__omp_critical___omp_lp_cond_{{.+}}
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}}.iv = #cir.int<0> : !s64i
// CIR:       cir.global "private"  internal  @__omp_lp_cond_{{.+}} = #cir.int<0> : !s32i
// CIR-LABEL: cir.func{{.*}} @_Z28simd_lastprivate_conditionalv(
// CIR:       %[[A:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["A", init]
// CIR:       omp.simd private(
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_{{.+}})
// CIR:       cir.if %{{.*}} {
// CIR:       cir.if %{{.*}} {
// CIR:       cir.get_global @__omp_lp_cond_{{.+}} : !cir.ptr<!s32i>
// CIR:       cir.store align(4) %{{.*}}, %[[A]] : !s32i, !cir.ptr<!s32i>

// LLVM:      @__omp_lp_cond_{{.+}}.iv = internal global i64 0
// LLVM:      @__omp_lp_cond_{{.+}} = internal global i32 0
// LLVM-LABEL: define dso_local void @_Z28simd_lastprivate_conditionalv()
// LLVM:      %[[A:.*]] = alloca i32, i64 1, align 4
// LLVM:      %[[LAST_A_VAL:.*]] = load i32, ptr @__omp_lp_cond_{{.+}}, align 4
// LLVM:      store i32 %[[LAST_A_VAL]], ptr %{{.*}}, align 4
// LLVM:      %[[A_PRIV_VAL:.*]] = load i32, ptr %{{.*}}, align 4
// LLVM:      store i32 %[[A_PRIV_VAL]], ptr %[[A]], align 4
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      store i64 %{{.*}}, ptr @__omp_lp_cond_{{.+}}.iv, align 8
// LLVM:      %[[LP_A:.*]] = load i32, ptr %{{.*}}, align 4
// LLVM:      store i32 %[[LP_A]], ptr @__omp_lp_cond_{{.+}}, align 4
// LLVM:      %[[LAST_IV_VAL:.*]] = load i64, ptr @__omp_lp_cond_{{.+}}.iv, align 8
// LLVM:      %[[CMP:.*]] = icmp sle i64 %[[LAST_IV_VAL]], %{{.*}}

// CIR-LABEL: cir.func{{.*}} @_ZN1S1fEv(
// CIR:       %[[THIS_SLOT:.*]] = cir.alloca !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!rec_S>>, ["this", init]
// CIR:       cir.store %arg0, %[[THIS_SLOT]]
// CIR:       %[[THIS:.*]] = cir.load %[[THIS_SLOT]] : !cir.ptr<!cir.ptr<!rec_S>>, !cir.ptr<!rec_S>
// CIR:       %[[X_SLOT:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["x"]
// CIR:       %[[X_FIELD:.*]] = cir.get_member %[[THIS]][0] {name = "x"} : !cir.ptr<!rec_S> -> !cir.ptr<!s32i>
// CIR-NEXT:  cir.store align(8) %[[X_FIELD]], %[[X_SLOT]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR:       %[[X_IN:.*]] = cir.load %[[X_SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR:       %[[X_OUT:.*]] = cir.load %[[X_SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR:       omp.simd private(@__omp_firstprivate_x_{{.+}} %[[X_IN]] -> %arg1 : !cir.ptr<!s32i>) {
// CIR:       omp.critical(@__omp_critical___omp_lp_cond_x_{{.+}})
// CIR:       cir.get_global @__omp_lp_cond_x_{{.+}} : !cir.ptr<!s32i>
// CIR:       cir.store align(4) %{{.*}}, %[[X_OUT]] : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: define dso_local void @_ZN1S1fEv(
// LLVM:      %[[THIS_SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM:      store ptr %0, ptr %[[THIS_SLOT]], align 8
// LLVM:      %[[THIS:.*]] = load ptr, ptr %[[THIS_SLOT]], align 8
// LLVM:      %[[X_SLOT:.*]] = alloca ptr, i64 1, align 8
// LLVM:      %[[X_FIELD:.*]] = getelementptr inbounds nuw %struct.S, ptr %[[THIS]], i32 0, i32 0
// LLVM-NEXT: store ptr %[[X_FIELD]], ptr %[[X_SLOT]], align 8
// LLVM:      %[[X_IN:.*]] = load ptr, ptr %[[X_SLOT]], align 8, !nonnull !{{.*}}, !align !{{.*}}
// LLVM:      %[[X_OUT:.*]] = load ptr, ptr %[[X_SLOT]], align 8, !nonnull !{{.*}}, !align !{{.*}}
// LLVM:      store i32 %{{.*}}, ptr %[[X_OUT]], align 4
// LLVM:      call void @__kmpc_critical_with_hint
// LLVM:      store i64 %{{.*}}, ptr @__omp_lp_cond_x_{{.+}}.iv, align 8
// LLVM:      store i32 %{{.*}}, ptr @__omp_lp_cond_x_{{.+}}, align 4

// OGCG-LABEL: define {{.*}} @_Z28simd_lastprivate_conditionalv(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(

// OGCG-LABEL: define {{.*}} @_ZN1S1fEv(
// OGCG: call void @__kmpc_critical(
// OGCG: call void @__kmpc_end_critical(
