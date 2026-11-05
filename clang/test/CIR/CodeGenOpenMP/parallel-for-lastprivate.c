// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void lastprivate_only(int *out) {
  int x;
#pragma omp parallel for lastprivate(x)
  for (int i = 0; i < 4; ++i)
    x = i;
  *out = x;
}

// CIR-LABEL: cir.func{{.*}} @lastprivate_only(
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
// CIR: omp.wsloop nowait private(@{{.*}} %{{.*}} -> %arg2 : !cir.ptr<!s32i>) {
// CIR: %[[IV:.+]] = builtin.unrealized_conversion_cast %arg3 : i32 to !s32i
// CIR: %[[UB:.+]] = builtin.unrealized_conversion_cast %{{.+}} : i32 to !s32i
// CIR: %[[STEP:.+]] = cir.const #cir.int<1> : !s32i
// CIR: %[[NEXT:.+]] = cir.binop(add, %[[IV]], %[[STEP]]) : !s32i
// CIR: %[[ISLAST:.+]] = cir.cmp(ge, %[[NEXT]], %[[UB]]) : !s32i, !cir.bool
// CIR: cir.if %[[ISLAST]] {
// CIR: %[[PRIV:.+]] = cir.load align(4) %arg2 : !cir.ptr<!s32i>, !s32i
// CIR: cir.store align(4) %[[PRIV]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: define dso_local void @lastprivate_only(
// LLVM: call void @__kmpc_for_static_init_4
// LLVM-NOT: call void @__kmpc_barrier
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// Body is wrapped in do_while(false) for continue support, adding extra blocks.
// LLVM: %[[PRIVVAL:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM-NEXT: store i32 %[[PRIVVAL]], ptr %{{.+}}, align 4
// do-while false condition:
// LLVM: br i1 false, label %{{.+}}, label %[[EXIT:.+]]
// lastprivate check after do-while exit:
// LLVM: %[[NEXT:.+]] = add i32 %{{.+}}, 1
// LLVM-NEXT: %[[ISLAST:.+]] = icmp sge i32 %[[NEXT]], 4
// LLVM-NEXT: br i1 %[[ISLAST]], label %[[COPY:.+]], label %[[CONT:.+]]
// LLVM: [[COPY]]:
// LLVM: %[[COPYVAL:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM: store i32 %[[COPYVAL]], ptr %{{.+}}, align 4

void first_and_lastprivate(int *out, int x) {
#pragma omp parallel for firstprivate(x) lastprivate(x)
  for (int i = 0; i < 4; ++i)
    x = i;
  *out = x;
}

// CIR-LABEL: cir.func{{.*}} @first_and_lastprivate(
// CIR: omp.parallel private(@{{.*}} %{{.*}} -> %arg2, @{{.*}} %{{.*}} -> %arg3 : !cir.ptr<!s32i>, !cir.ptr<!s32i>) {
// CIR: omp.wsloop nowait private(@{{.*}} %arg2 -> %arg4 : !cir.ptr<!s32i>) private_barrier {
// CIR: cir.if %{{.+}} {
// CIR: %[[PRIV2:.+]] = cir.load align(4) %arg4 : !cir.ptr<!s32i>, !s32i
// CIR: cir.store align(4) %[[PRIV2]], %{{.+}} : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: define dso_local void @first_and_lastprivate(
// LLVM: call void @__kmpc_barrier
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i32 %{{.+}}, ptr %{{.+}}, align 4
// Body is wrapped in do_while(false) for continue support, adding extra blocks.
// LLVM: %[[PRIVVAL2:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM-NEXT: store i32 %[[PRIVVAL2]], ptr %{{.+}}, align 4
// do-while false condition:
// LLVM: br i1 false, label %{{.+}}, label %{{.+}}
// lastprivate check after do-while exit:
// LLVM: %[[NEXT2:.+]] = add i32 %{{.+}}, 1
// LLVM-NEXT: %[[ISLAST2:.+]] = icmp sge i32 %[[NEXT2]], 4
// LLVM-NEXT: br i1 %[[ISLAST2]], label %[[COPY2:.+]], label %[[CONT2:.+]]
// LLVM: [[COPY2]]:
// LLVM: %[[COPYVAL2:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM: store i32 %[[COPYVAL2]], ptr %{{.+}}, align 4

// OGCG-LABEL: define dso_local void @lastprivate_only(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @lastprivate_only.omp_outlined

// OGCG-LABEL: define internal void @lastprivate_only.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define dso_local void @first_and_lastprivate(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.+}}, i32 1, ptr @first_and_lastprivate.omp_outlined

// OGCG-LABEL: define internal void @first_and_lastprivate.omp_outlined(
// OGCG: call void @__kmpc_barrier(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
