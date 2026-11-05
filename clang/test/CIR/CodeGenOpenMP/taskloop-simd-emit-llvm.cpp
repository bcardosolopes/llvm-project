// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM

long long step();

void taskloop_simd_descending_nonunit(int *p) {
#pragma omp taskloop simd
  for (int i = 15; i >= 3; i -= 4)
    p[0] += i;
}

void taskloop_simd_linear_nonzero_start(int *p) {
  long long k = step();
#pragma omp taskloop simd linear(k : 3)
  for (int i = 10; i < 18; ++i)
    p[i] += k;
}

void taskloop_simd_if(float *p, int n) {
#pragma omp taskloop simd if(simd : n)
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

// LLVM-LABEL: define dso_local void @_Z32taskloop_simd_descending_nonunitPi(
// LLVM: %[[LB_GEP:.*]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 0
// LLVM: store i64 0, ptr %[[LB_GEP]], align 8
// LLVM: %[[UB_GEP:.*]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 1
// LLVM: store i64 3, ptr %[[UB_GEP]], align 8
// LLVM: %[[STEP_GEP:.*]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 2
// LLVM: store i64 1, ptr %[[STEP_GEP]], align 8

// LLVM-LABEL: define internal void @_Z32taskloop_simd_descending_nonunitPi..omp_par(
// LLVM: omp_loop.body:
// LLVM: %[[USERMUL:.*]] = mul nsw i32 %{{.*}}, 4
// LLVM: %[[USERIV:.*]] = sub nsw i32 15, %[[USERMUL]]
// LLVM: store i32 %[[USERIV]], ptr %{{.*}}, align 4, !llvm.access.group !{{.*}}

// LLVM-LABEL: define dso_local void @_Z{{[0-9]+}}taskloop_simd_linear_nonzero_startPi(
// LLVM: %[[LB_GEP:.*]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 0
// LLVM: store i64 0, ptr %[[LB_GEP]], align 8
// LLVM: %[[UB_GEP:.*]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 1
// LLVM: store i64 7, ptr %[[UB_GEP]], align 8
// LLVM: %[[STEP_GEP:.*]] = getelementptr {{.*}}, ptr %structArg, i32 0, i32 2
// LLVM: store i64 1, ptr %[[STEP_GEP]], align 8

// LLVM-LABEL: define internal void @_Z{{[0-9]+}}taskloop_simd_linear_nonzero_startPi..omp_par(
// LLVM: omp_loop.preheader:
// LLVM: %[[TASKLB:.*]] = trunc i64 %loadgep_lb.val to i32
// LLVM: omp_loop.body:
// LLVM: %[[TASKIV:.*]] = add i32 %omp_loop.iv, %[[TASKLB]]
// LLVM: %[[USERSPAN:.*]] = mul i32 %[[TASKIV]], 1
// LLVM: %[[USERIV:.*]] = add i32 %[[USERSPAN]], 10
// LLVM: %[[LINEARIV:.*]] = sext i32 %[[TASKIV]] to i64
// LLVM: %[[LINEARMUL:.*]] = mul i64 %[[LINEARIV]], 3

// LLVM-LABEL: define dso_local void @_Z{{[0-9]+}}taskloop_simd_ifPfi(
// LLVM: store i32 %1, ptr %[[N_ADDR:.*]], align 4
// LLVM: %[[N_VAL:.*]] = load i32, ptr %[[N_ADDR]], align 4
// LLVM: %[[IFCOND:.*]] = icmp ne i32 %[[N_VAL]], 0
// LLVM: %[[IFCOND_BYTE:.*]] = zext i1 %[[IFCOND]] to i8
// LLVM: store i8 %[[IFCOND_BYTE]], ptr %[[IFCAP:.*]], align 1
// LLVM: %[[IFCOND_RELOAD:.*]] = load i8, ptr %[[IFCAP]], align 1
// LLVM: %[[IFCOND_I1:.*]] = trunc i8 %[[IFCOND_RELOAD]] to i1
// LLVM: store i1 %[[IFCOND_I1]], ptr %{{.*}}, align 1

// LLVM-LABEL: define internal void @_Z{{[0-9]+}}taskloop_simd_ifPfi..omp_par(
// LLVM: %[[TASK_IF_GEP:.*]] = getelementptr { i64, i64, i64, ptr, i1 }, ptr %1, i32 0, i32 4
// LLVM: %[[TASK_IF:.*]] = load i1, ptr %[[TASK_IF_GEP]], align 1
// LLVM: omp_loop.preheader:
// LLVM: %[[IFTASKLB:.*]] = trunc i64 %loadgep_lb.val to i32
// LLVM: omp_loop.body:
// LLVM: %[[IFTASKIV:.*]] = add i32 %omp_loop.iv, %[[IFTASKLB]]
// LLVM: br i1 %[[TASK_IF]], label %simd.if.then, label %simd.if.else
// LLVM-COUNT-2: mul i32 %[[IFTASKIV]], 1
