// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

long long step();

void for_simd_linear_copyback(int *p) {
  long long k = step();
#pragma omp for simd linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void parallel_for_simd_linear_copyback(int *p) {
  long long k = step();
#pragma omp parallel for simd linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

// LLVM-LABEL: define dso_local void @_Z{{[0-9]+}}for_simd_linear_copybackPi(
// LLVM: %[[LASTITER:.*]] = alloca i32, align 4
// LLVM: %[[LINEAR_PRIV:.*]] = alloca i64, align 8
// LLVM: %[[LINEAR_RESULT:.*]] = alloca i64, align 8
// LLVM: %[[GTID_PRE:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call void @__kmpc_barrier(ptr @{{.*}}, i32 %[[GTID_PRE]])
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 34, ptr %[[LASTITER]], ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: call void @__kmpc_for_static_fini(ptr @{{.*}}, i32 %{{.*}})
// LLVM: br label %omp_loop.linear_finalization
// LLVM: omp_loop.linear_finalization:
// LLVM: %[[LASTITER_VAL:.*]] = load i32, ptr %[[LASTITER]], align 4
// LLVM: %[[IS_LAST:.*]] = icmp ne i32 %[[LASTITER_VAL]], 0
// LLVM: br i1 %[[IS_LAST]], label %[[COPYBACK:.*]], label %[[LINEAR_EXIT:.*]]
// LLVM: [[COPYBACK]]:
// LLVM: %[[LINEAR_FINAL:.*]] = load i64, ptr %[[LINEAR_RESULT]], align 8
// LLVM: store i64 %[[LINEAR_FINAL]], ptr %{{.*}}, align 8
// LLVM: [[LINEAR_EXIT]]:
// LLVM: %[[GTID_POST:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call void @__kmpc_barrier(ptr @{{.*}}, i32 %[[GTID_POST]])

// LLVM-LABEL: define internal void @_Z{{[0-9]+}}parallel_for_simd_linear_copybackPi..omp_par(
// LLVM: %[[K_ADDR:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM: %[[LASTITER:.*]] = alloca i32, align 4
// LLVM: %[[LINEAR_PRIV:.*]] = alloca i64, align 8
// LLVM: %[[LINEAR_RESULT:.*]] = alloca i64, align 8
// LLVM: %[[GTID_PRE:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: call void @__kmpc_barrier(ptr @{{.*}}, i32 %[[GTID_PRE]])
// LLVM: call void @__kmpc_for_static_init_4(ptr @{{.*}}, i32 %{{.*}}, i32 34, ptr %[[LASTITER]], ptr %{{.*}}, ptr %{{.*}}, ptr %{{.*}}, i32 1, i32 1)
// LLVM: call void @__kmpc_for_static_fini(ptr @{{.*}}, i32 %{{.*}})
// LLVM: br label %omp_loop.linear_finalization
// LLVM: omp_loop.linear_finalization:
// LLVM: %[[LASTITER_VAL:.*]] = load i32, ptr %[[LASTITER]], align 4
// LLVM: %[[IS_LAST:.*]] = icmp ne i32 %[[LASTITER_VAL]], 0
// LLVM: br i1 %[[IS_LAST]], label %[[COPYBACK:.*]], label %[[LINEAR_EXIT:.*]]
// LLVM: [[LINEAR_EXIT]]:
// LLVM: br label %omp_loop.after
// LLVM: [[COPYBACK]]:
// LLVM: %[[LINEAR_FINAL:.*]] = load i64, ptr %[[LINEAR_RESULT]], align 8
// LLVM: store i64 %[[LINEAR_FINAL]], ptr %[[K_ADDR]], align 8

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}for_simd_linear_copybackPi(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}parallel_for_simd_linear_copybackPi(
// OGCG: call void {{.*}} @__kmpc_fork_call(

// OGCG-LABEL: define internal {{.*}} @_Z{{[0-9]+}}parallel_for_simd_linear_copybackPi.omp_outlined(
// OGCG: call void @__kmpc_for_static_init_4(
// OGCG: call void @__kmpc_for_static_fini(
