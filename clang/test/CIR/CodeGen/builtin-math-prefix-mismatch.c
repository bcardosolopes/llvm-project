// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Guard against prefix-only matching in CIR builtin dispatch. Names that only
// begin with frexp/modf/sincos/sincospi should not be rewritten to LLVM math
// intrinsics unless they are exact builtin spellings.
extern double __builtin_modf_extra(double, double *);
extern float __builtin_modff_extra(float, float *);
extern long double __builtin_modfl_extra(long double, long double *);
extern void __builtin_sincos_extra(double, double *, double *);
extern void __builtin_sincosf_extra(float, float *, float *);
extern void __builtin_sincosl_extra(long double, long double *, long double *);
extern void __builtin_sincospi_extra(double, double *, double *);
extern void __builtin_sincospif_extra(float, float *, float *);
extern void __builtin_sincospil_extra(long double, long double *,
                                      long double *);
extern double __builtin_frexp_extra(double, int *);
extern float __builtin_frexpf_extra(float, int *);
extern long double __builtin_frexpl_extra(long double, int *);

double test_builtin_modf_prefix_mismatch(double x, double *iptr) {
  return __builtin_modf_extra(x, iptr);
}

// CIR-LABEL: @test_builtin_modf_prefix_mismatch
// CIR: call double @__builtin_modf_extra(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { double, double } @llvm.modf.f64

// OGCG-LABEL: @test_builtin_modf_prefix_mismatch
// OGCG: call double @__builtin_modf_extra(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { double, double } @llvm.modf.f64

float test_builtin_modff_prefix_mismatch(float x, float *iptr) {
  return __builtin_modff_extra(x, iptr);
}

// CIR-LABEL: @test_builtin_modff_prefix_mismatch
// CIR: call float @__builtin_modff_extra(float noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { float, float } @llvm.modf.f32

// OGCG-LABEL: @test_builtin_modff_prefix_mismatch
// OGCG: call float @__builtin_modff_extra(float noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { float, float } @llvm.modf.f32

long double test_builtin_modfl_prefix_mismatch(long double x,
                                               long double *iptr) {
  return __builtin_modfl_extra(x, iptr);
}

// CIR-LABEL: @test_builtin_modfl_prefix_mismatch
// CIR: call x86_fp80 @__builtin_modfl_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { x86_fp80, x86_fp80 } @llvm.modf.f80

// OGCG-LABEL: @test_builtin_modfl_prefix_mismatch
// OGCG: call x86_fp80 @__builtin_modfl_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { x86_fp80, x86_fp80 } @llvm.modf.f80

void test_builtin_sincos_prefix_mismatch(double x, double *sinp, double *cosp) {
  __builtin_sincos_extra(x, sinp, cosp);
}

// CIR-LABEL: @test_builtin_sincos_prefix_mismatch
// CIR: call void @__builtin_sincos_extra(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { double, double } @llvm.sincos.f64

// OGCG-LABEL: @test_builtin_sincos_prefix_mismatch
// OGCG: call void @__builtin_sincos_extra(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { double, double } @llvm.sincos.f64

void test_builtin_sincosf_prefix_mismatch(float x, float *sinp, float *cosp) {
  __builtin_sincosf_extra(x, sinp, cosp);
}

// CIR-LABEL: @test_builtin_sincosf_prefix_mismatch
// CIR: call void @__builtin_sincosf_extra(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { float, float } @llvm.sincos.f32

// OGCG-LABEL: @test_builtin_sincosf_prefix_mismatch
// OGCG: call void @__builtin_sincosf_extra(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { float, float } @llvm.sincos.f32

void test_builtin_sincosl_prefix_mismatch(long double x, long double *sinp,
                                          long double *cosp) {
  __builtin_sincosl_extra(x, sinp, cosp);
}

// CIR-LABEL: @test_builtin_sincosl_prefix_mismatch
// CIR: call void @__builtin_sincosl_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { x86_fp80, x86_fp80 } @llvm.sincos.f80

// OGCG-LABEL: @test_builtin_sincosl_prefix_mismatch
// OGCG: call void @__builtin_sincosl_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { x86_fp80, x86_fp80 } @llvm.sincos.f80

void test_builtin_sincospi_prefix_mismatch(double x, double *sinp,
                                           double *cosp) {
  __builtin_sincospi_extra(x, sinp, cosp);
}

// CIR-LABEL: @test_builtin_sincospi_prefix_mismatch
// CIR: call void @__builtin_sincospi_extra(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { double, double } @llvm.sincospi.f64

// OGCG-LABEL: @test_builtin_sincospi_prefix_mismatch
// OGCG: call void @__builtin_sincospi_extra(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { double, double } @llvm.sincospi.f64

void test_builtin_sincospif_prefix_mismatch(float x, float *sinp, float *cosp) {
  __builtin_sincospif_extra(x, sinp, cosp);
}

// CIR-LABEL: @test_builtin_sincospif_prefix_mismatch
// CIR: call void @__builtin_sincospif_extra(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { float, float } @llvm.sincospi.f32

// OGCG-LABEL: @test_builtin_sincospif_prefix_mismatch
// OGCG: call void @__builtin_sincospif_extra(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { float, float } @llvm.sincospi.f32

void test_builtin_sincospil_prefix_mismatch(long double x, long double *sinp,
                                            long double *cosp) {
  __builtin_sincospil_extra(x, sinp, cosp);
}

// CIR-LABEL: @test_builtin_sincospil_prefix_mismatch
// CIR: call void @__builtin_sincospil_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { x86_fp80, x86_fp80 } @llvm.sincospi.f80

// OGCG-LABEL: @test_builtin_sincospil_prefix_mismatch
// OGCG: call void @__builtin_sincospil_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { x86_fp80, x86_fp80 } @llvm.sincospi.f80

double test_builtin_frexp_prefix_mismatch(double x, int *exp) {
  return __builtin_frexp_extra(x, exp);
}

// CIR-LABEL: @test_builtin_frexp_prefix_mismatch
// CIR: call double @__builtin_frexp_extra(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { double, i32 } @llvm.frexp.f64.i32

// OGCG-LABEL: @test_builtin_frexp_prefix_mismatch
// OGCG: call double @__builtin_frexp_extra(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { double, i32 } @llvm.frexp.f64.i32

float test_builtin_frexpf_prefix_mismatch(float x, int *exp) {
  return __builtin_frexpf_extra(x, exp);
}

// CIR-LABEL: @test_builtin_frexpf_prefix_mismatch
// CIR: call float @__builtin_frexpf_extra(float noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { float, i32 } @llvm.frexp.f32.i32

// OGCG-LABEL: @test_builtin_frexpf_prefix_mismatch
// OGCG: call float @__builtin_frexpf_extra(float noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { float, i32 } @llvm.frexp.f32.i32

long double test_builtin_frexpl_prefix_mismatch(long double x, int *exp) {
  return __builtin_frexpl_extra(x, exp);
}

// CIR-LABEL: @test_builtin_frexpl_prefix_mismatch
// CIR: call x86_fp80 @__builtin_frexpl_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: call { x86_fp80, i32 } @llvm.frexp.f80.i32

// OGCG-LABEL: @test_builtin_frexpl_prefix_mismatch
// OGCG: call x86_fp80 @__builtin_frexpl_extra(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: call { x86_fp80, i32 } @llvm.frexp.f80.i32
