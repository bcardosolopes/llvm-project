// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -fno-clangir-direct-lowering -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -fno-clangir-direct-lowering -emit-mlir=core %s -o - | FileCheck %s --check-prefix=MLIR
// REQUIRES: cir-support

// CHECK-DAG: omp.private {type = firstprivate} @{{.*}} : !s32i copy {
// CHECK-DAG: ^bb0(%[[COPY0_SRC:arg[0-9]+]]: !cir.ptr<!s32i> {{.*}}, %[[COPY0_DST:arg[0-9]+]]: !cir.ptr<!s32i> {{.*}}):
// CHECK-DAG: cir.load align(4) %[[COPY0_SRC]]
// CHECK-DAG: cir.store align(4) {{.*}}, %[[COPY0_DST]]
// CHECK-DAG: omp.yield(%[[COPY0_DST]] : !cir.ptr<!s32i>)
// CHECK-DAG: omp.private {type = firstprivate} @{{.*}} : !s32i copy {
// CHECK-DAG: ^bb0(%[[COPY1_SRC:arg[0-9]+]]: !cir.ptr<!s32i> {{.*}}, %[[COPY1_DST:arg[0-9]+]]: !cir.ptr<!s32i> {{.*}}):
// CHECK-DAG: cir.load align(4) %[[COPY1_SRC]]
// CHECK-DAG: cir.store align(4) {{.*}}, %[[COPY1_DST]]
// CHECK-DAG: omp.yield(%[[COPY1_DST]] : !cir.ptr<!s32i>)
// CHECK-LABEL: cir.func no_inline optnone dso_local @foo(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %[[QPRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[PPRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[IVPRIV:arg[0-9]+]] : !cir.ptr<!s32i>, !cir.ptr<!s32i>, !cir.ptr<!s32i>) {
// CHECK: cir.scope {
// CHECK: %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// CHECK: %[[LB:.*]] = builtin.unrealized_conversion_cast %[[ZERO]] : !s32i to i32
// CHECK: %[[QLOAD:.*]] = cir.load align(4) %[[QPRIV]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[UB:.*]] = builtin.unrealized_conversion_cast %[[QLOAD]] : !s32i to i32
// CHECK: %[[STEP:.*]] = builtin.unrealized_conversion_cast {{.*}} to i32
// CHECK: omp.wsloop nowait {
// CHECK: omp.loop_nest (%[[IV:arg[0-9]+]]) : i32 = (%[[LB]]) to (%[[UB]]) step (%[[STEP]]) {
// CHECK: %[[IVCIR:.*]] = builtin.unrealized_conversion_cast %[[IV]] : i32 to !s32i
// CHECK: cir.store align(4) %[[IVCIR]], %[[IVPRIV]] : !s32i, !cir.ptr<!s32i>
// CHECK: %[[IVAL:.*]] = cir.load align(4) %[[IVPRIV]] : !cir.ptr<!s32i>, !s32i
// CHECK: %[[PVAL:.*]] = cir.load align(4) %[[PPRIV]] : !cir.ptr<!s32i>, !s32i
// CHECK: cir.call @work2(%[[IVAL]], %[[PVAL]]) : (!s32i, !s32i) -> ()
// CHECK: omp.yield
// CHECK: omp.terminator

// LLVM-LABEL: define dso_local void @foo(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 2, ptr @foo..omp_par, i32 %{{.*}}, i32 %{{.*}})
// LLVM-LABEL: define internal void @foo..omp_par(
// LLVM-SAME: ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM: call void @__kmpc_for_static_fini(
// LLVM-NOT: call void @__kmpc_barrier(
// LLVM: call void @work2(i32

void work2(int, int);
void work3(unsigned, int);

void foo(int q) {
  int p = 2;
#pragma omp parallel for firstprivate(p, q)
  for (int i = 0; i < q; i++)
    work2(i, p);
}

// CHECK-LABEL: cir.func no_inline optnone dso_local @foo_unsigned(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %[[QPRIVU:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[PPRIVU:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[IVPRIVU:arg[0-9]+]] : !cir.ptr<!u32i>, !cir.ptr<!s32i>, !cir.ptr<!u32i>) {
// CHECK: %[[ZEROU:.*]] = cir.const #cir.int<0> : !u32i
// CHECK: %[[LBU:.*]] = builtin.unrealized_conversion_cast %[[ZEROU]] : !u32i to i32
// CHECK: %[[QLOADU:.*]] = cir.load align(4) %[[QPRIVU]] : !cir.ptr<!u32i>, !u32i
// CHECK: %[[UBU:.*]] = builtin.unrealized_conversion_cast %[[QLOADU]] : !u32i to i32
// CHECK: omp.wsloop nowait {
// CHECK: omp.loop_nest (%[[IVU:arg[0-9]+]]) : i32 = (%[[LBU]]) to (%[[UBU]]) step (%{{.*}}) {
// CHECK: %[[IVUCIR:.*]] = builtin.unrealized_conversion_cast %[[IVU]] : i32 to !u32i
// CHECK: cir.store align(4) %[[IVUCIR]], %[[IVPRIVU]] : !u32i, !cir.ptr<!u32i>
// CHECK: cir.call @work3

// LLVM-LABEL: define dso_local void @foo_unsigned(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 2, ptr @foo_unsigned..omp_par, i32 %{{.*}}, i32 %{{.*}})
// LLVM-LABEL: define internal void @foo_unsigned..omp_par(
// LLVM-SAME: ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// LLVM: call void @__kmpc_for_static_init_4u(
// LLVM: declare !callback ![[CALLBACK:[0-9]+]] void @__kmpc_fork_call(ptr, i32, ptr, ...)
// LLVM: ![[CALLBACK]] = !{![[ENTRY:[0-9]+]]}
// LLVM: ![[ENTRY]] = !{i64 2, i64 -1, i64 -1, i1 true}

// MLIR-DAG: omp.private {type = firstprivate} @{{.*}} : !s32i copy {
// MLIR-DAG: ^bb0(%[[S_COPY_SRC:arg[0-9]+]]: !cir.ptr<!s32i>, %[[S_COPY_DST:arg[0-9]+]]: !cir.ptr<!s32i>):
// MLIR-DAG: %[[S_DST_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[S_COPY_DST]] : !cir.ptr<!s32i> to memref<?xi32>
// MLIR-DAG: %[[S_SRC_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[S_COPY_SRC]] : !cir.ptr<!s32i> to memref<?xi32>
// MLIR-DAG: %[[S_PRIV_VAL:.*]] = memref.load %[[S_SRC_MEMREF]][%{{.*}}] : memref<?xi32>
// MLIR-DAG: memref.store %[[S_PRIV_VAL]], %[[S_DST_MEMREF]][%{{.*}}] : memref<?xi32>
// MLIR-DAG: omp.yield(%[[S_COPY_DST]] : !cir.ptr<!s32i>)
// MLIR-DAG: omp.private {type = firstprivate} @{{.*}} : !u32i copy {
// MLIR-DAG: ^bb0(%[[U_COPY_SRC:arg[0-9]+]]: !cir.ptr<!u32i>, %[[U_COPY_DST:arg[0-9]+]]: !cir.ptr<!u32i>):
// MLIR-DAG: %[[U_DST_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[U_COPY_DST]] : !cir.ptr<!u32i> to memref<?xi32>
// MLIR-DAG: %[[U_SRC_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[U_COPY_SRC]] : !cir.ptr<!u32i> to memref<?xi32>
// MLIR-DAG: %[[U_PRIV_VAL:.*]] = memref.load %[[U_SRC_MEMREF]][%{{.*}}] : memref<?xi32>
// MLIR-DAG: memref.store %[[U_PRIV_VAL]], %[[U_DST_MEMREF]][%{{.*}}] : memref<?xi32>
// MLIR-DAG: omp.yield(%[[U_COPY_DST]] : !cir.ptr<!u32i>)
// MLIR-LABEL: func.func @foo(
// MLIR: omp.parallel private(@{{.*}} %{{.*}} -> %[[Q_PRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[P_PRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[IV_PRIV:arg[0-9]+]] : !cir.ptr<!s32i>, !cir.ptr<!s32i>, !cir.ptr<!s32i>) {
// MLIR-DAG: %[[Q_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[Q_PRIV]] : !cir.ptr<!s32i> to memref<?xi32>
// MLIR-DAG: %[[P_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[P_PRIV]] : !cir.ptr<!s32i> to memref<?xi32>
// MLIR-DAG: %[[IV_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[IV_PRIV]] : !cir.ptr<!s32i> to memref<?xi32>
// MLIR: omp.wsloop nowait {
// MLIR: omp.loop_nest (%[[IV:arg[0-9]+]]) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {
// MLIR: func.call @work2(%{{.*}}, %{{.*}}) : (i32, i32) -> ()
// MLIR-LABEL: func.func @foo_unsigned(
// MLIR: omp.parallel private(@{{.*}} %{{.*}} -> %[[UQ_PRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[UP_PRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[UIV_PRIV:arg[0-9]+]] : !cir.ptr<!u32i>, !cir.ptr<!s32i>, !cir.ptr<!u32i>) {
// MLIR-DAG: %[[UQ_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[UQ_PRIV]] : !cir.ptr<!u32i> to memref<?xi32>
// MLIR-DAG: %[[UP_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[UP_PRIV]] : !cir.ptr<!s32i> to memref<?xi32>
// MLIR-DAG: %[[UIV_MEMREF:.*]] = builtin.unrealized_conversion_cast %[[UIV_PRIV]] : !cir.ptr<!u32i> to memref<?xi32>
// MLIR: omp.wsloop nowait {
// MLIR: omp.loop_nest (%[[UIV:arg[0-9]+]]) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {
// MLIR: func.call @work3(%{{.*}}, %{{.*}}) : (i32, i32) -> ()

void foo_unsigned(unsigned q) {
  int p = 2;
#pragma omp parallel for firstprivate(p, q)
  for (unsigned i = 0; i < q; i++)
    work3(i, p);
}
