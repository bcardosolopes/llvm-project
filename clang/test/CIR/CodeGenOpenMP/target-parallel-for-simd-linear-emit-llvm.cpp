// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

long long step();
double *g_ptr;

void target_parallel_for_simd_linear_copyback(int *p) {
  long long k = step();
#pragma omp target parallel for simd map(tofrom : p[0:1]) linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void target_parallel_for_simd_pointer_linear_copyback(int *p) {
#pragma omp target parallel for simd map(tofrom : p[0:1]) linear(g_ptr)
  for (int i = 0; i < 4; ++i)
    p[0] += i;
}

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_for_simd_linear_copybackPi_l{{[0-9]+}}..omp_par(
// LLVM: %[[K_ADDR:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM: %[[LASTITER:.*]] = alloca i32, align 4
// LLVM: %[[LINEAR_PRIV:.*]] = alloca i64, align 8
// LLVM: %[[LINEAR_RESULT:.*]] = alloca i64, align 8
// LLVM: %[[GTID_PRE:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call void @__kmpc_barrier(ptr @{{.*}}, i32 %[[GTID_PRE]])
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 34, ptr %[[LASTITER]], ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: call void @__kmpc_for_static_fini(ptr @{{.*}}, i32 %{{.*}})
// LLVM-NEXT: br label %omp_loop.linear_finalization
// LLVM: omp_loop.linear_finalization:
// LLVM-NEXT: %[[LASTITER_VAL:.*]] = load i32, ptr %[[LASTITER]], align 4
// LLVM-NEXT: %[[IS_LAST:.*]] = icmp ne i32 %[[LASTITER_VAL]], 0
// LLVM-NEXT: br i1 %[[IS_LAST]], label %[[COPYBACK:.*]], label %[[LINEAR_EXIT:.*]]
// LLVM: [[LINEAR_EXIT]]:
// LLVM-NEXT: br label %omp_loop.after
// LLVM: [[COPYBACK]]:
// LLVM-NEXT: %[[LINEAR_FINAL:.*]] = load i64, ptr %[[LINEAR_RESULT]], align 8
// LLVM-NEXT: store i64 %[[LINEAR_FINAL]], ptr %[[K_ADDR]], align 8

// LLVM-LABEL: define internal void @__omp_offloading_{{.*}}_Z{{[0-9]+}}target_parallel_for_simd_pointer_linear_copybackPi_l{{[0-9]+}}..omp_par(
// LLVM: %[[G_PTR_ADDR:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM: %[[PTR_LASTITER:.*]] = alloca i32, align 4
// LLVM: %[[PTR_LINEAR_PRIV:.*]] = alloca ptr, align 8
// LLVM: %[[PTR_LINEAR_RESULT:.*]] = alloca ptr, align 8
// LLVM: %[[PTR_GTID_PRE:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call void @__kmpc_barrier(ptr @{{.*}}, i32 %[[PTR_GTID_PRE]])
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 34, ptr %[[PTR_LASTITER]], ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: call void @__kmpc_for_static_fini(ptr @{{.*}}, i32 %{{.*}})
// LLVM-NEXT: br label %omp_loop.linear_finalization
// LLVM: omp_loop.linear_finalization:
// LLVM-NEXT: %[[PTR_LASTITER_VAL:.*]] = load i32, ptr %[[PTR_LASTITER]], align 4
// LLVM-NEXT: %[[PTR_IS_LAST:.*]] = icmp ne i32 %[[PTR_LASTITER_VAL]], 0

// OGCG-LABEL: @_Z40target_parallel_for_simd_linear_copybackPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z48target_parallel_for_simd_pointer_linear_copybackPi(
// OGCG: call i32 @__tgt_target_kernel(
// LLVM-NEXT: br i1 %[[PTR_IS_LAST]], label %[[PTR_COPYBACK:.*]], label %[[PTR_LINEAR_EXIT:.*]]
// LLVM: [[PTR_LINEAR_EXIT]]:
// LLVM-NEXT: br label %omp_loop.after
// LLVM: [[PTR_COPYBACK]]:
// LLVM-NEXT: %[[PTR_LINEAR_FINAL:.*]] = load ptr, ptr %[[PTR_LINEAR_RESULT]], align 8
// LLVM-NEXT: store ptr %[[PTR_LINEAR_FINAL]], ptr %[[G_PTR_ADDR]], align 8
