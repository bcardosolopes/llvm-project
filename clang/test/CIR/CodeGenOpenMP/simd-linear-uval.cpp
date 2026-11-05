// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=50 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void simd_linear_uval_ref(float *a) {
  long long val = 0;
  long long &k = val;
#pragma omp simd linear(uval(k) : 3)
  for (int i = 10; i > 1; i--)
    a[k]++;
}

// CIR-LABEL: cir.func{{.*}} @_Z20simd_linear_uval_refPf(
// CIR: %[[K_REF:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!s64i>>, !cir.ptr<!s64i> {cir.nonnull, cir.ref_align = 8 : i64}
// CIR: omp.simd linear(%[[K_REF]] = %{{.*}} : !cir.ptr<!s64i>) {
// CIR: } {linear_var_types = [i64]}

// LLVM-LABEL: define{{.*}} @_Z20simd_linear_uval_refPf(
// LLVM: %[[K_REF:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM: %[[LINEAR_PRIV:.*]] = alloca i64, align 8
// LLVM: %[[LINEAR_RESULT:.*]] = alloca i64, align 8
// LLVM: %[[K_INIT:.*]] = load i64, ptr %[[K_REF]], align 8
// LLVM: store i64 %[[K_INIT]], ptr %[[LINEAR_PRIV]], align 8
// LLVM: %[[LINEAR_FINAL:.*]] = load i64, ptr %[[LINEAR_RESULT]], align 8
// LLVM: store i64 %[[LINEAR_FINAL]], ptr %[[K_REF]], align 8

// OGCG-LABEL: define {{.*}} @_Z20simd_linear_uval_refPf(
// OGCG: br label %omp.inner.for.cond
