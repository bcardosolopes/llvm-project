// Verify that `#pragma omp declare simd` attaches the Vector Function ABI
// variant names to the scalar function so the loop vectorizer can emit
// vectorized calls. ClangIR previously dropped these attributes silently.

// RUN: %clang_cc1 -fopenmp -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=X86
// RUN: %clang_cc1 -fopenmp -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=X86
// RUN: %clang_cc1 -fopenmp -triple aarch64-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=AARCH64
// RUN: %clang_cc1 -fopenmp -triple aarch64-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=AARCH64

#pragma omp declare simd
#pragma omp declare simd simdlen(4) notinbranch
#pragma omp declare simd simdlen(8) inbranch
float compute(float a, float b) { return a * b + a; }

#pragma omp declare simd uniform(mul) linear(i : 1)
double scaled(double *p, int i, double mul) { return p[i] * mul; }

// X86: define {{.*}}float @compute(
// X86: define {{.*}}double @scaled(
//
// The undecorated pragma derives the VLEN from the characteristic data type
// (float -> 4/8/8/16 lanes across SSE/AVX/AVX2/AVX512); simdlen(4)/(8) pin the
// vector length; inbranch/notinbranch pick the M-vs-N mask letter.
// X86-DAG: "_ZGVbM4vv_compute"
// X86-DAG: "_ZGVbN4vv_compute"
// X86-DAG: "_ZGVbM8vv_compute"
// X86-DAG: "_ZGVcN4vv_compute"
// X86-DAG: "_ZGVeM16vv_compute"
// X86-DAG: "_ZGVeN16vv_compute"
//
// uniform(mul) + linear(i:1) produce the "vlu" parameter-kind sequence.
// X86-DAG: "_ZGVbM2vlu_scaled"
// X86-DAG: "_ZGVbN2vlu_scaled"
// X86-DAG: "_ZGVeM8vlu_scaled"

// AARCH64: define {{.*}}float @compute(
// AARCH64: define {{.*}}double @scaled(
//
// NEON (ISA letter 'n') derives Advanced SIMD lane counts from the narrowest
// data size when simdlen is absent, and honors simdlen/branch otherwise.
// AARCH64-DAG: "_ZGVnM2vv_compute"
// AARCH64-DAG: "_ZGVnN2vv_compute"
// AARCH64-DAG: "_ZGVnM4vv_compute"
// AARCH64-DAG: "_ZGVnM8vv_compute"
// AARCH64-DAG: "_ZGVnM2vlu_scaled"
// AARCH64-DAG: "_ZGVnN2vlu_scaled"
