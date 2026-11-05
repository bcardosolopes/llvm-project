// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp-simd -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

double *g_ptr;

void simd_pointer_linear() {
#pragma omp simd linear(g_ptr)
  for (int i = 0; i < 4; ++i)
    *g_ptr++ = 0.0;
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}simd_pointer_linearv(
// CIR: omp.simd {{.*}}linear(
// LLVM-LABEL: define{{.*}} @_Z{{[0-9]+}}simd_pointer_linearv(
// LLVM: %[[STEPBYTES:.*]] = mul i32 %{{.*}}, 8
// LLVM: %[[STEPIDX:.*]] = sext i32 %[[STEPBYTES]] to i64
// LLVM: %[[LINEARGEP:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %[[STEPIDX]]
// LLVM: store ptr %[[LINEARGEP]], ptr %{{.*}}

// OGCG-LABEL: define {{.*}} @_Z{{[0-9]+}}simd_pointer_linearv(
// OGCG: br label %omp.inner.for.cond
