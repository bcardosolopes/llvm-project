// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void use_int(int);

void test_parallel_reduction(int x) {
  int sum = x;
#pragma omp parallel reduction(+: sum)
  {
    sum += 1;
    use_int(sum);
  }
}

// CIR-LABEL: cir.func{{.*}} @test_parallel_reduction(
// CIR: omp.parallel reduction(@__omp_reduction_add_s32 %{{.*}} -> %arg{{.*}} : !cir.ptr<!s32i>) {
// CIR: %[[SUM:.*]] = cir.load align(4) %arg{{.*}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[NEXT:.*]] = cir.binop(add, %[[SUM]], %{{.*}}) nsw : !s32i
// CIR: cir.store align(4) %[[NEXT]], %arg{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR: %[[LOAD:.*]] = cir.load align(4) %arg{{.*}} : !cir.ptr<!s32i>, !s32i
// CIR: cir.call @use_int(%[[LOAD]]) : (!s32i) -> ()

// LLVM-LABEL: define dso_local void @test_parallel_reduction(
// LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @test_parallel_reduction..omp_par, ptr %{{.*}})
// LLVM-LABEL: define internal void @test_parallel_reduction..omp_par(
// LLVM: call void @use_int(i32 noundef %{{.*}})
// LLVM: %[[STATUS:.*]] = call i32 @__kmpc_reduce(ptr @{{.*}}, i32 %{{.*}}, i32 1, i64 8, ptr %{{.*}}, ptr @.omp.reduction.func, ptr @{{.*}})
// LLVM: switch i32 %[[STATUS]]
// LLVM: call void @__kmpc_end_reduce(ptr @{{.*}}, i32 %{{.*}}, ptr @{{.*}})

// Verify that parallel-for reduction uses nowait and does not emit a
// redundant barrier. The enclosing parallel region provides the
// implicit synchronization, so the worksharing loop is marked nowait.
int test_parallel_for_reduction(int *arr, int n) {
  int sum = 0;
#pragma omp parallel for reduction(+: sum)
  for (int i = 0; i < n; i++) {
    sum += arr[i];
  }
  return sum;
}

// CIR-LABEL: cir.func{{.*}} @test_parallel_for_reduction(
// CIR: omp.parallel
// CIR: omp.wsloop nowait reduction(@__omp_reduction_add_s32
// CIR: omp.loop_nest

// LLVM-LABEL: define internal void @test_parallel_for_reduction..omp_par(
// LLVM: call i32 @__kmpc_reduce_nowait(
// LLVM: call void @__kmpc_end_reduce_nowait(
// LLVM-NOT: call void @__kmpc_barrier(

// OGCG-LABEL: define {{.*}} @test_parallel_reduction(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 1, ptr @test_parallel_reduction.omp_outlined,
// OGCG-LABEL: define internal void @test_parallel_reduction.omp_outlined(
// OGCG:         call void @use_int(i32 noundef %{{.*}})
// OGCG:         call i32 @__kmpc_reduce_nowait(
// OGCG:         switch i32
// OGCG:         call void @__kmpc_end_reduce_nowait(

// OGCG-LABEL: define {{.*}} @test_parallel_for_reduction(
// OGCG:         call void {{.*}} @__kmpc_fork_call({{.*}}, i32 3, ptr @test_parallel_for_reduction.omp_outlined,
// OGCG-LABEL: define internal void @test_parallel_for_reduction.omp_outlined(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call i32 @__kmpc_reduce_nowait(
// OGCG:         call void @__kmpc_end_reduce_nowait(
