// RUN: %clang_cc1 -emit-cir -fclangir -triple x86_64-unknown-linux-gnu -ffast-math %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -emit-llvm -fclangir -triple x86_64-unknown-linux-gnu -ffast-math %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu -ffast-math %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Verify FP comparisons carry FastMathFlags through to LLVM IR,
// FP casts carry them at the CIR level, and vector FP comparisons carry FMF.

typedef float float4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));

// --- FP comparisons: FMF on cir.cmp, forwarded to fcmp ---

// CIR-LABEL: @test_fcmp_lt
// CIR:         cir.cmp(lt, {{.*}}) nnan|ninf|nsz|arcp|afn|reassoc : !cir.float
// LLVM-LABEL: define {{.*}} @test_fcmp_lt(
// LLVM:         fcmp reassoc nnan ninf nsz arcp afn olt float
// OGCG-LABEL: define {{.*}} @test_fcmp_lt(
// OGCG:         fcmp reassoc nnan ninf nsz arcp afn olt float
int test_fcmp_lt(float a, float b) { return a < b; }

// CIR-LABEL: @test_fcmp_eq
// CIR:         cir.cmp(eq, {{.*}}) nnan|ninf|nsz|arcp|afn|reassoc : !cir.float
// LLVM-LABEL: define {{.*}} @test_fcmp_eq(
// LLVM:         fcmp reassoc nnan ninf nsz arcp afn oeq float
// OGCG-LABEL: define {{.*}} @test_fcmp_eq(
// OGCG:         fcmp reassoc nnan ninf nsz arcp afn oeq float
int test_fcmp_eq(float a, float b) { return a == b; }

// --- FP casts: FMF on cir.cast, now forwarded to fpext/fptrunc ---

// CIR-LABEL: @test_fpext
// CIR:         cir.cast floating {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : !cir.float -> !cir.double
// LLVM-LABEL: define {{.*}} @test_fpext(
// LLVM:         fpext reassoc nnan ninf nsz arcp afn float
// OGCG-LABEL: define {{.*}} @test_fpext(
// OGCG:         fpext reassoc nnan ninf nsz arcp afn float
double test_fpext(float a) { return a; }

// CIR-LABEL: @test_fptrunc
// CIR:         cir.cast floating {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : !cir.double -> !cir.float
// LLVM-LABEL: define {{.*}} @test_fptrunc(
// LLVM:         fptrunc reassoc nnan ninf nsz arcp afn double
// OGCG-LABEL: define {{.*}} @test_fptrunc(
// OGCG:         fptrunc reassoc nnan ninf nsz arcp afn double
float test_fptrunc(double a) { return a; }

// --- Vector FP comparisons: FMF on cir.vec.cmp, forwarded to fcmp ---

// CIR-LABEL: @test_vec_fcmp_lt
// CIR:         cir.vec.cmp(lt, {{.*}}) nnan|ninf|nsz|arcp|afn|reassoc
// LLVM-LABEL: define {{.*}} @test_vec_fcmp_lt(
// LLVM:         fcmp reassoc nnan ninf nsz arcp afn olt <4 x float>
// OGCG-LABEL: define {{.*}} @test_vec_fcmp_lt(
// OGCG:         fcmp reassoc nnan ninf nsz arcp afn olt <4 x float>
int4 test_vec_fcmp_lt(float4 a, float4 b) { return a < b; }

// CIR-LABEL: @test_vec_fcmp_eq
// CIR:         cir.vec.cmp(eq, {{.*}}) nnan|ninf|nsz|arcp|afn|reassoc
// LLVM-LABEL: define {{.*}} @test_vec_fcmp_eq(
// LLVM:         fcmp reassoc nnan ninf nsz arcp afn oeq <4 x float>
// OGCG-LABEL: define {{.*}} @test_vec_fcmp_eq(
// OGCG:         fcmp reassoc nnan ninf nsz arcp afn oeq <4 x float>
int4 test_vec_fcmp_eq(float4 a, float4 b) { return a == b; }
