// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -fno-clangir-direct-lowering -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
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
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %[[PPRIV:arg[0-9]+]], @{{.*}} %{{.*}} -> %[[QPRIV:arg[0-9]+]] : !cir.ptr<!s32i>, !cir.ptr<!s32i>) {
// CHECK: cir.scope {
// CHECK: %{{.*}} = cir.load align(4) %[[PPRIV]] : !cir.ptr<!s32i>, !s32i
// CHECK: %{{.*}} = cir.load align(4) %[[QPRIV]] : !cir.ptr<!s32i>, !s32i
// CHECK: cir.call @work1(

// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 2, ptr @foo..omp_par, i32 %{{.*}}, i32 %{{.*}})
// LLVM-LABEL: define internal void @foo..omp_par(
// LLVM-SAME: ptr noalias noundef %{{.*}}, ptr noalias noundef %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// LLVM: declare !callback ![[CALLBACK:[0-9]+]] void @__kmpc_fork_call(ptr, i32, ptr, ...)
// LLVM: ![[CALLBACK]] = !{![[ENTRY:[0-9]+]]}
// LLVM: ![[ENTRY]] = !{i64 2, i64 -1, i64 -1, i1 true}

void work1(int, int);

void foo(int q) {
  int p = 2;
#pragma omp parallel firstprivate(q, p)
  work1(p, q);
}
