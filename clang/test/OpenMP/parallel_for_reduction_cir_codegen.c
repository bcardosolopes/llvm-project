// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -fno-clangir-direct-lowering -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// REQUIRES: cir-support

// CHECK-LABEL: omp.declare_reduction @__omp_reduction_max_u32 : !u32i init {
// CHECK: %{{.*}} = cir.const #cir.int<0> : !u32i
// CHECK: omp.yield(%{{.*}} : !u32i)
// CHECK-LABEL: } atomic {
// CHECK: %{{.*}} = cir.atomic.fetch max relaxed fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!u32i>, !u32i) -> !u32i

// CHECK-LABEL: omp.declare_reduction @__omp_reduction_min_s32 : !s32i init {
// CHECK: %{{.*}} = cir.const #cir.int<2147483647> : !s32i
// CHECK: omp.yield(%{{.*}} : !s32i)
// CHECK-LABEL: } atomic {
// CHECK: %{{.*}} = cir.atomic.fetch min relaxed fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i

// CHECK-LABEL: omp.declare_reduction @__omp_reduction_add_s32 : !s32i init {
// CHECK: %{{.*}} = cir.const #cir.int<0> : !s32i
// CHECK: omp.yield(%{{.*}} : !s32i)
// CHECK-LABEL: } combiner {
// CHECK: %{{.*}} = cir.binop(add, %{{.*}}, %{{.*}}) nsw : !s32i
// CHECK: omp.yield(%{{.*}} : !s32i)
// CHECK-LABEL: } atomic {
// CHECK: %{{.*}} = cir.atomic.fetch add relaxed fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i

// CHECK-LABEL: cir.func no_inline optnone dso_local @foo(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait reduction(@__omp_reduction_add_s32 %{{.*}} -> %[[RED:arg[0-9]+]] : !cir.ptr<!s32i>) {
// CHECK: %{{.*}} = cir.load align(4) %[[RED]] : !cir.ptr<!s32i>, !s32i
// CHECK: cir.store align(4) %{{.*}}, %[[RED]] : !s32i, !cir.ptr<!s32i>

// CHECK-LABEL: cir.func no_inline optnone dso_local @foo_min(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait reduction(@__omp_reduction_min_s32 %{{.*}} -> %[[MINRED:arg[0-9]+]] : !cir.ptr<!s32i>) {

// CHECK-LABEL: cir.func no_inline optnone dso_local @foo_umax(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait reduction(@__omp_reduction_max_u32 %{{.*}} -> %[[MAXRED:arg[0-9]+]] : !cir.ptr<!u32i>) {

// LLVM-LABEL: define dso_local void @foo(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM: call i32 @__kmpc_reduce_nowait(
// LLVM: atomicrmw add ptr %{{.*}}, i32 %{{.*}} monotonic, align 4
// LLVM: add nsw i32

// LLVM-LABEL: define dso_local void @foo_min(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM: call i32 @__kmpc_reduce_nowait(
// LLVM: atomicrmw min ptr %{{.*}}, i32 %{{.*}} monotonic, align 4

// LLVM-LABEL: define dso_local void @foo_umax(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM: call i32 @__kmpc_reduce_nowait(
// LLVM: atomicrmw umax ptr %{{.*}}, i32 %{{.*}} monotonic, align 4

void sink(int);
void sinku(unsigned);

void foo(int n) {
  int sum = 0;
#pragma omp parallel for reduction(+:sum)
  for (int i = 0; i < n; ++i)
    sum += i;
  sink(sum);
}

void foo_min(int n) {
  int best = n;
#pragma omp parallel for reduction(min:best)
  for (int i = 0; i < n; ++i)
    if (i < best)
      best = i;
  sink(best);
}

void foo_umax(int n) {
  unsigned best = 0;
#pragma omp parallel for reduction(max:best)
  for (int i = 0; i < n; ++i)
    if ((unsigned)i > best)
      best = (unsigned)i;
  sinku(best);
}
