// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-contract=on -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR-ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-contract=on -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM-ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-contract=on -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG-ON

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-contract=off -fclangir -emit-cir %s -o %t.off.cir
// RUN: FileCheck --input-file=%t.off.cir %s --check-prefix=CIR-OFF

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-contract=fast -fclangir -emit-cir %s -o %t.fast.cir
// RUN: FileCheck --input-file=%t.fast.cir %s --check-prefix=CIR-FAST

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -ffp-contract=on -fclangir -emit-llvm %s -o %t.fastmath.ll
// RUN: FileCheck --input-file=%t.fastmath.ll %s --check-prefix=LLVM-FASTMATH
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -ffp-contract=on -emit-llvm %s -o %t.ogcg.fastmath.ll
// RUN: FileCheck --input-file=%t.ogcg.fastmath.ll %s --check-prefix=OGCG-FASTMATH

// Test that -ffp-contract=on emits cir.fmuladd / @llvm.fmuladd for
// a*b+c patterns within a single expression.

float fmuladd_add(float a, float b, float c) {
  return a * b + c;
}

// CIR-ON: cir.func {{.*}} @fmuladd_add
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_add
// LLVM-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// OGCG-ON: define {{.*}} @fmuladd_add
// OGCG-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// -ffp-contract=off: no fmuladd, separate fmul + fadd.
// CIR-OFF: cir.func {{.*}} @fmuladd_add
// CIR-OFF:   cir.binop(mul, {{.*}}) : !cir.float
// CIR-OFF:   cir.binop(add, {{.*}}) : !cir.float

// -ffp-contract=fast: no fmuladd, uses contract flag.
// CIR-FAST: cir.func {{.*}} @fmuladd_add
// CIR-FAST:   cir.binop(mul, {{.*}}) contract : !cir.float
// CIR-FAST:   cir.binop(add, {{.*}}) contract : !cir.float

// -ffast-math -ffp-contract=on: fmuladd with fast-math flags.
// LLVM-FASTMATH: define {{.*}} @fmuladd_add
// LLVM-FASTMATH:   call reassoc nnan ninf nsz arcp afn float @llvm.fmuladd.f32(
// OGCG-FASTMATH: define {{.*}} @fmuladd_add
// OGCG-FASTMATH:   call reassoc nnan ninf nsz arcp afn float @llvm.fmuladd.f32(

float fmuladd_sub(float a, float b, float c) {
  return a * b - c;
}

// CIR-ON: cir.func {{.*}} @fmuladd_sub
// CIR-ON:   cir.unary(minus, {{.*}}) : !cir.float
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_sub
// LLVM-ON:   fneg float
// LLVM-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// OGCG-ON: define {{.*}} @fmuladd_sub
// OGCG-ON:   fneg float
// OGCG-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

float fmuladd_rhs_mul(float a, float b, float c) {
  return c + a * b;
}

// CIR-ON: cir.func {{.*}} @fmuladd_rhs_mul
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_rhs_mul
// LLVM-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// OGCG-ON: define {{.*}} @fmuladd_rhs_mul
// OGCG-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// Separate statements: mul result stored, so no fmuladd.
float no_fmuladd_separate(float a, float b, float c) {
  float t = a * b;
  return t + c;
}

// CIR-ON: cir.func {{.*}} @no_fmuladd_separate
// CIR-ON:   cir.binop(mul, {{.*}}) : !cir.float
// CIR-ON:   cir.binop(add, {{.*}}) : !cir.float

// LLVM-ON: define {{.*}} @no_fmuladd_separate
// LLVM-ON:   fmul float
// LLVM-ON:   fadd float

double fmuladd_double(double a, double b, double c) {
  return a * b + c;
}

// CIR-ON: cir.func {{.*}} @fmuladd_double
// CIR-ON:   cir.fmuladd {{.*}} : !cir.double

// LLVM-ON: define {{.*}} @fmuladd_double
// LLVM-ON:   call double @llvm.fmuladd.f64(double %{{.*}}, double %{{.*}}, double %{{.*}})

// OGCG-ON: define {{.*}} @fmuladd_double
// OGCG-ON:   call double @llvm.fmuladd.f64(double %{{.*}}, double %{{.*}}, double %{{.*}})

// Negated LHS multiply: (-a) * b + c → fmuladd(-a, b, c)
float fmuladd_neg_lhs(float a, float b, float c) {
  return (-a) * b + c;
}

// CIR-ON: cir.func {{.*}} @fmuladd_neg_lhs
// CIR-ON:   cir.unary(minus, {{.*}}) : !cir.float
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_neg_lhs
// LLVM-ON:   fneg float
// LLVM-ON:   call float @llvm.fmuladd.f32(

// Compound assignment with fmuladd: c += a * b
float fmuladd_compound(float a, float b, float c) {
  c += a * b;
  return c;
}

// CIR-ON: cir.func {{.*}} @fmuladd_compound
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_compound
// LLVM-ON:   call float @llvm.fmuladd.f32(

// Subtraction with fmul on RHS: c - a*b → fmuladd(-a, b, c)
float fmuladd_rhs_sub(float a, float b, float c) {
  return c - a * b;
}

// CIR-ON: cir.func {{.*}} @fmuladd_rhs_sub
// CIR-ON:   cir.unary(minus, {{.*}}) : !cir.float
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_rhs_sub
// LLVM-ON:   fneg float
// LLVM-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// OGCG-ON: define {{.*}} @fmuladd_rhs_sub
// OGCG-ON:   fneg float
// OGCG-ON:   call float @llvm.fmuladd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

// Compound subtraction with fmuladd: c -= a * b → fmuladd(-a, b, c)
float fmuladd_compound_sub(float a, float b, float c) {
  c -= a * b;
  return c;
}

// CIR-ON: cir.func {{.*}} @fmuladd_compound_sub
// CIR-ON:   cir.unary(minus, {{.*}}) : !cir.float
// CIR-ON:   cir.fmuladd {{.*}} : !cir.float

// LLVM-ON: define {{.*}} @fmuladd_compound_sub
// LLVM-ON:   fneg float
// LLVM-ON:   call float @llvm.fmuladd.f32(

// OGCG-ON: define {{.*}} @fmuladd_compound_sub
// OGCG-ON:   fneg float
// OGCG-ON:   call float @llvm.fmuladd.f32(
