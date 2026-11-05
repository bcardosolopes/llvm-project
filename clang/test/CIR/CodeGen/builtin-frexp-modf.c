// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefixes=LLVM,LLVMDECLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o %t-strict-cir.ll
// RUN: FileCheck --input-file=%t-strict-cir.ll %s -check-prefix=STRICT-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -ffp-exception-behavior=strict -emit-llvm %s -o %t-strict-ogcg.ll
// RUN: FileCheck --input-file=%t-strict-ogcg.ll %s -check-prefix=STRICT-OGCG

// Test __builtin_frexp and __builtin_modf family of builtins.
// These return one result and store the other through a pointer.
extern double modf(double, double *);
extern float modff(float, float *);
extern long double modfl(long double, long double *);
extern double frexp(double, int *);
extern float frexpf(float, int *);
extern long double frexpl(long double, int *);

// LLVMDECLS: ModuleID = '{{.*}}clang/test/CIR/CodeGen/builtin-frexp-modf.c'
// LLVMDECLS-NOT: declare double @__builtin_frexp(
// LLVMDECLS-NOT: declare float @__builtin_frexpf(
// LLVMDECLS-NOT: declare x86_fp80 @__builtin_frexpl(
// LLVMDECLS-NOT: declare fp128 @__builtin_frexpf128(
// LLVMDECLS-NOT: declare double @__builtin_modf(
// LLVMDECLS-NOT: declare float @__builtin_modff(
// LLVMDECLS-NOT: declare x86_fp80 @__builtin_modfl(

double test_frexp(double val, int *exp) {
  return __builtin_frexp(val, exp);
}

// CIR-LABEL: @test_frexp
// CIR: cir.call @__builtin_frexp({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexp
// LLVM: call { double, i32 } @llvm.frexp.f64.i32(double %{{.*}})
// LLVM: extractvalue { double, i32 } %{{.*}}, 0
// LLVM: extractvalue { double, i32 } %{{.*}}, 1
// LLVM: store i32

// OGCG-LABEL: @test_frexp
// OGCG: call { double, i32 } @llvm.frexp.f64.i32(double %{{.*}})

// STRICT-CIR-LABEL: @test_frexp(
// STRICT-CIR: tail call { double, i32 } @llvm.frexp.f64.i32(double %{{.*}}) #[[STRICTCALL_CIR:[0-9]+]]

// STRICT-OGCG-LABEL: @test_frexp(
// STRICT-OGCG: tail call { double, i32 } @llvm.frexp.f64.i32(double %{{.*}}) #[[STRICTCALL_OGCG:[0-9]+]]

float test_frexpf(float val, int *exp) {
  return __builtin_frexpf(val, exp);
}

// CIR-LABEL: @test_frexpf
// CIR: cir.call @__builtin_frexpf({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexpf
// LLVM: call { float, i32 } @llvm.frexp.f32.i32(float %{{.*}})

// OGCG-LABEL: @test_frexpf
// OGCG: call { float, i32 } @llvm.frexp.f32.i32(float %{{.*}})

// STRICT-CIR-LABEL: @test_frexpf(
// STRICT-CIR: tail call { float, i32 } @llvm.frexp.f32.i32(float %{{.*}}) #[[STRICTCALL_CIR]]

// STRICT-OGCG-LABEL: @test_frexpf(
// STRICT-OGCG: tail call { float, i32 } @llvm.frexp.f32.i32(float %{{.*}}) #[[STRICTCALL_OGCG]]

long double test_frexpl(long double val, int *exp) {
  return __builtin_frexpl(val, exp);
}

// CIR-LABEL: @test_frexpl
// CIR: cir.call @__builtin_frexpl({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexpl
// LLVM: call { x86_fp80, i32 } @llvm.frexp.f80.i32(x86_fp80 %{{.*}})

// OGCG-LABEL: @test_frexpl
// OGCG: call { x86_fp80, i32 } @llvm.frexp.f80.i32(x86_fp80 %{{.*}})

// STRICT-CIR-LABEL: @test_frexpl(
// STRICT-CIR: tail call { x86_fp80, i32 } @llvm.frexp.f80.i32(x86_fp80 %{{.*}}) #[[STRICTCALL_CIR]]

// STRICT-OGCG-LABEL: @test_frexpl(
// STRICT-OGCG: tail call { x86_fp80, i32 } @llvm.frexp.f80.i32(x86_fp80 %{{.*}}) #[[STRICTCALL_OGCG]]

__float128 test_frexpf128(__float128 val, int *exp) {
  return __builtin_frexpf128(val, exp);
}

// CIR-LABEL: @test_frexpf128
// CIR: cir.call @__builtin_frexpf128({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexpf128
// LLVM: call { fp128, i32 } @llvm.frexp.f128.i32(fp128 %{{.*}})

// OGCG-LABEL: @test_frexpf128
// OGCG: call { fp128, i32 } @llvm.frexp.f128.i32(fp128 %{{.*}})

// STRICT-CIR-LABEL: @test_frexpf128(
// STRICT-CIR: tail call { fp128, i32 } @llvm.frexp.f128.i32(fp128 %{{.*}}) #[[STRICTCALL_CIR]]

// STRICT-OGCG-LABEL: @test_frexpf128(
// STRICT-OGCG: tail call { fp128, i32 } @llvm.frexp.f128.i32(fp128 %{{.*}}) #[[STRICTCALL_OGCG]]

long double test_frexpl_alias(long double val, int *exp) {
  return frexpl(val, exp);
}

// CIR-LABEL: @test_frexpl_alias
// CIR: cir.call @frexpl({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexpl_alias
// LLVM: call x86_fp80 @frexpl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: @test_frexpl_alias
// OGCG: call x86_fp80 @frexpl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})

double test_frexp_alias(double val, int *exp) {
  return frexp(val, exp);
}

// CIR-LABEL: @test_frexp_alias
// CIR: cir.call @frexp({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexp_alias
// LLVM: call double @frexp(double noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: @test_frexp_alias
// OGCG: call double @frexp(double noundef %{{.*}}, ptr noundef %{{.*}})

float test_frexpf_alias(float val, int *exp) {
  return frexpf(val, exp);
}

// CIR-LABEL: @test_frexpf_alias
// CIR: cir.call @frexpf({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_frexpf_alias
// LLVM: call float @frexpf(float noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: @test_frexpf_alias
// OGCG: call float @frexpf(float noundef %{{.*}}, ptr noundef %{{.*}})

double test_modf(double val, double *iptr) {
  return __builtin_modf(val, iptr);
}

// CIR-LABEL: @test_modf
// CIR: cir.call @__builtin_modf({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_modf
// LLVM: call { double, double } @llvm.modf.f64(double %{{.*}})
// LLVM: extractvalue { double, double } %{{.*}}, 0
// LLVM: extractvalue { double, double } %{{.*}}, 1
// LLVM: store double

// OGCG-LABEL: @test_modf
// OGCG: call { double, double } @llvm.modf.f64(double %{{.*}})

float test_modff(float val, float *iptr) {
  return __builtin_modff(val, iptr);
}

// CIR-LABEL: @test_modff
// CIR: cir.call @__builtin_modff({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_modff
// LLVM: call { float, float } @llvm.modf.f32(float %{{.*}})

// OGCG-LABEL: @test_modff
// OGCG: call { float, float } @llvm.modf.f32(float %{{.*}})

double test_modf_alias(double val, double *iptr) {
  return modf(val, iptr);
}

// CIR-LABEL: @test_modf_alias
// CIR: cir.call @modf({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_modf_alias
// LLVM: call { double, double } @llvm.modf.f64(double %{{.*}})

// OGCG-LABEL: @test_modf_alias
// OGCG: call { double, double } @llvm.modf.f64(double %{{.*}})

float test_modff_alias(float val, float *iptr) {
  return modff(val, iptr);
}

// CIR-LABEL: @test_modff_alias
// CIR: cir.call @modff({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_modff_alias
// LLVM: call { float, float } @llvm.modf.f32(float %{{.*}})

// OGCG-LABEL: @test_modff_alias
// OGCG: call { float, float } @llvm.modf.f32(float %{{.*}})

long double test_modfl(long double val, long double *iptr) {
  return __builtin_modfl(val, iptr);
}

// CIR-LABEL: @test_modfl
// CIR: cir.call @__builtin_modfl({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_modfl
// LLVM: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80 %{{.*}})

// OGCG-LABEL: @test_modfl
// OGCG: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80 %{{.*}})

long double test_modfl_alias(long double val, long double *iptr) {
  return modfl(val, iptr);
}

// CIR-LABEL: @test_modfl_alias
// CIR: cir.call @modfl({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_modfl_alias
// LLVM: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80 %{{.*}})

// OGCG-LABEL: @test_modfl_alias
// OGCG: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80 %{{.*}})

// STRICT-CIR: attributes #[[STRICTCALL_CIR]] = { strictfp }
// STRICT-OGCG: attributes #[[STRICTCALL_OGCG]] = { strictfp }
