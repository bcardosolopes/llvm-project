// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -fno-clangir-direct-lowering -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// REQUIRES: cir-support

// CHECK-DAG: omp.private {type = private} @{{.*}} : !cir.ptr<!cir.float>
// CHECK-DAG: omp.private {type = private} @{{.*}} : !rec_S

// CHECK-LABEL: cir.func no_inline optnone dso_local @if_num_threads(
// CHECK: omp.parallel if(%{{.*}}) num_threads(%{{.*}} : i32) private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait schedule(static) {

// CHECK-LABEL: cir.func no_inline optnone dso_local @private_static(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait schedule(static) private(@{{.*}} %{{.*}} -> %[[APRIV:arg[0-9]+]] : !cir.ptr<!cir.ptr<!cir.float>>) {
// CHECK: %[[APTR:.*]] = cir.load align(4) %[[APRIV]] : !cir.ptr<!cir.ptr<!cir.float>>, !cir.ptr<!cir.float>

// CHECK-LABEL: cir.func no_inline optnone dso_local @dynamic_chunk(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait schedule(dynamic = %{{.*}} : i32) {

// CHECK-LABEL: cir.func no_inline optnone dso_local @monotonic_dynamic_chunk(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait schedule(dynamic = %{{.*}} : i32, monotonic) {

// CHECK-LABEL: cir.func no_inline optnone dso_local @default_shared(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait {

// CHECK-LABEL: cir.func no_inline optnone dso_local @private_struct(
// CHECK: omp.parallel private(@{{.*}} %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>) {
// CHECK: omp.wsloop nowait private(@{{.*}} %{{.*}} -> %[[SPRIV:arg[0-9]+]] : !cir.ptr<!rec_S>) {
// CHECK: cir.copy %[[SPRIV]] to %{{.*}} {dst_alignment = 4 : i64, src_alignment = 4 : i64} : !cir.ptr<!rec_S>

// LLVM-LABEL: define dso_local void @if_num_threads(
// LLVM: call void @__kmpc_push_num_threads(
// LLVM: call void @__kmpc_fork_call_if(
// LLVM-NOT: call void @__kmpc_barrier(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define dso_local void @private_static(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM: call void @__kmpc_for_static_init_4(
// LLVM-NOT: call void @__kmpc_barrier(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define dso_local void @dynamic_chunk(
// LLVM: call void @__kmpc_dispatch_init_4(
// LLVM: call i32 @__kmpc_dispatch_next_4(
// LLVM-NOT: call void @__kmpc_barrier(

// LLVM-LABEL: define dso_local void @monotonic_dynamic_chunk(
// LLVM: call void @__kmpc_dispatch_init_4(
// LLVM: call i32 @__kmpc_dispatch_next_4(
// LLVM-NOT: call void @__kmpc_barrier(

// LLVM-LABEL: define dso_local void @default_shared(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM-NOT: call void @__kmpc_barrier(
// LLVM: call void @__kmpc_for_static_fini(

// LLVM-LABEL: define dso_local void @private_struct(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM-LABEL: define internal void @private_struct..omp_par(
// LLVM: %omp.private.alloc{{.*}} = alloca %struct.S, align 8
// LLVM: call void @__kmpc_for_static_fini(
// LLVM-NOT: call void @__kmpc_barrier(
// LLVM: call void @llvm.memcpy{{.*}}(ptr align 4 {{.*}}, ptr align 4 %omp.private.alloc{{.*}}, i{{32|64}} 8, i1 false)

void if_num_threads(float *a, float *b, float *c, float *d, int nt, int cond) {
#pragma omp parallel for if(parallel : cond) num_threads(nt) schedule(static)
  for (int i = 0; i < 100; ++i)
    a[i] = b[i] * c[i] * d[i];
}

void private_static(float *a, float *b, float *c, float *d) {
#pragma omp parallel for schedule(static) private(a)
  for (int i = 0; i < 100; ++i)
    a[i] = b[i] * c[i] * d[i];
}

void dynamic_chunk(float *a, float *b, float *c, float *d) {
#pragma omp parallel for schedule(dynamic, 4)
  for (int i = 0; i < 100; ++i)
    a[i] = b[i] * c[i] * d[i];
}

void monotonic_dynamic_chunk(float *a, float *b) {
#pragma omp parallel for schedule(monotonic : dynamic, 4)
  for (int i = 0; i < 100; ++i)
    a[i] = b[i];
}

void default_shared(float *a, float *b, float *c, float *d) {
#pragma omp parallel for default(shared) shared(a, b, c, d)
  for (int i = 0; i < 100; ++i)
    a[i] = b[i] * c[i] * d[i];
}

struct S {
  int x;
  int y;
};

void private_struct(struct S *out) {
  struct S s;
#pragma omp parallel for private(s)
  for (int i = 0; i < 4; ++i)
    out[i] = s;
}
