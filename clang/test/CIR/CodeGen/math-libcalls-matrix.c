// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-implicit-function-declaration -w \
// RUN:   -fclangir -emit-llvm -disable-llvm-passes %s -o %t.cir-noerrno.ll
// RUN: FileCheck --check-prefix=CIR-NOERR --input-file=%t.cir-noerrno.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-implicit-function-declaration -w \
// RUN:   -emit-llvm -disable-llvm-passes %s -o %t.ogcg-noerrno.ll
// RUN: FileCheck --check-prefix=OGCG-NOERR --input-file=%t.ogcg-noerrno.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-implicit-function-declaration -w \
// RUN:   -fclangir -emit-llvm -disable-llvm-passes -fmath-errno %s -o %t.cir-errno.ll
// RUN: FileCheck --check-prefix=CIR-ERRNO --input-file=%t.cir-errno.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-implicit-function-declaration -w \
// RUN:   -emit-llvm -disable-llvm-passes -fmath-errno %s -o %t.ogcg-errno.ll
// RUN: FileCheck --check-prefix=OGCG-ERRNO --input-file=%t.ogcg-errno.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-implicit-function-declaration -w \
// RUN:   -fclangir -emit-llvm -disable-llvm-passes -ffp-exception-behavior=maytrap \
// RUN:   %s -o %t.cir-maytrap.ll
// RUN: FileCheck --check-prefix=CIR-MAYTRAP --input-file=%t.cir-maytrap.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-implicit-function-declaration -w \
// RUN:   -emit-llvm -disable-llvm-passes -ffp-exception-behavior=maytrap %s \
// RUN:   -o %t.ogcg-maytrap.ll
// RUN: FileCheck --check-prefix=OGCG-MAYTRAP --input-file=%t.ogcg-maytrap.ll %s

void test_frexp_family(double xd, float xf, long double xl, int *ip,
                       double *dp, float *fp, long double *lp) {
  *dp = frexp(xd, ip);
  *fp = frexpf(xf, ip);
  *lp = frexpl(xl, ip);
}

double test_modf_family(double xd, float xf, long double xl, double *dp,
                        float *fp, long double *lp) {
  double r0 = modf(xd, dp);
  float r1 = modff(xf, fp);
  long double r2 = modfl(xl, lp);
  return r0 + r1 + r2;
}

void test_sincos_family(double xd, float xf, long double xl, double *dp,
                        float *fp, long double *lp) {
  sincos(xd, dp, dp);
  sincosf(xf, fp, fp);
  sincosl(xl, lp, lp);
}

// CIR-NOERR-LABEL: define {{.*}} @test_frexp_family(
// CIR-NOERR: call double @frexp(double noundef
// CIR-NOERR: call float @frexpf(float noundef
// CIR-NOERR: call x86_fp80 @frexpl(x86_fp80 noundef
// CIR-NOERR-LABEL: define {{.*}} @test_modf_family(
// CIR-NOERR: call { double, double } @llvm.modf.f64(double
// CIR-NOERR: call { float, float } @llvm.modf.f32(float
// CIR-NOERR: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80
// CIR-NOERR-LABEL: define {{.*}} @test_sincos_family(
// CIR-NOERR: call { double, double } @llvm.sincos.f64(double
// CIR-NOERR: call { float, float } @llvm.sincos.f32(float
// CIR-NOERR: call { x86_fp80, x86_fp80 } @llvm.sincos.f80(x86_fp80

// OGCG-NOERR-LABEL: define {{.*}} @test_frexp_family(
// OGCG-NOERR: call double @frexp(double noundef
// OGCG-NOERR: call float @frexpf(float noundef
// OGCG-NOERR: call x86_fp80 @frexpl(x86_fp80 noundef
// OGCG-NOERR-LABEL: define {{.*}} @test_modf_family(
// OGCG-NOERR: call { double, double } @llvm.modf.f64(double
// OGCG-NOERR: call { float, float } @llvm.modf.f32(float
// OGCG-NOERR: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80
// OGCG-NOERR-LABEL: define {{.*}} @test_sincos_family(
// OGCG-NOERR: call { double, double } @llvm.sincos.f64(double
// OGCG-NOERR: call { float, float } @llvm.sincos.f32(float
// OGCG-NOERR: call { x86_fp80, x86_fp80 } @llvm.sincos.f80(x86_fp80

// CIR-ERRNO-LABEL: define {{.*}} @test_frexp_family(
// CIR-ERRNO: call double @frexp(double noundef
// CIR-ERRNO: call float @frexpf(float noundef
// CIR-ERRNO: call x86_fp80 @frexpl(x86_fp80 noundef
// CIR-ERRNO-LABEL: define {{.*}} @test_modf_family(
// CIR-ERRNO: call { double, double } @llvm.modf.f64(double
// CIR-ERRNO: call { float, float } @llvm.modf.f32(float
// CIR-ERRNO: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80
// CIR-ERRNO-LABEL: define {{.*}} @test_sincos_family(
// CIR-ERRNO: call void @sincos(double noundef
// CIR-ERRNO: call void @sincosf(float noundef
// CIR-ERRNO: call void @sincosl(x86_fp80 noundef

// OGCG-ERRNO-LABEL: define {{.*}} @test_frexp_family(
// OGCG-ERRNO: call double @frexp(double noundef
// OGCG-ERRNO: call float @frexpf(float noundef
// OGCG-ERRNO: call x86_fp80 @frexpl(x86_fp80 noundef
// OGCG-ERRNO-LABEL: define {{.*}} @test_modf_family(
// OGCG-ERRNO: call { double, double } @llvm.modf.f64(double
// OGCG-ERRNO: call { float, float } @llvm.modf.f32(float
// OGCG-ERRNO: call { x86_fp80, x86_fp80 } @llvm.modf.f80(x86_fp80
// OGCG-ERRNO-LABEL: define {{.*}} @test_sincos_family(
// OGCG-ERRNO: call void @sincos(double noundef
// OGCG-ERRNO: call void @sincosf(float noundef
// OGCG-ERRNO: call void @sincosl(x86_fp80 noundef

// CIR-MAYTRAP-LABEL: define {{.*}} @test_frexp_family(
// CIR-MAYTRAP: call double @frexp(double noundef
// CIR-MAYTRAP: call float @frexpf(float noundef
// CIR-MAYTRAP: call x86_fp80 @frexpl(x86_fp80 noundef
// CIR-MAYTRAP-LABEL: define {{.*}} @test_modf_family(
// CIR-MAYTRAP: call double @modf(double noundef
// CIR-MAYTRAP: call float @modff(float noundef
// CIR-MAYTRAP: call x86_fp80 @modfl(x86_fp80 noundef
// CIR-MAYTRAP-LABEL: define {{.*}} @test_sincos_family(
// CIR-MAYTRAP: call void @sincos(double noundef
// CIR-MAYTRAP: call void @sincosf(float noundef
// CIR-MAYTRAP: call void @sincosl(x86_fp80 noundef

// OGCG-MAYTRAP-LABEL: define {{.*}} @test_frexp_family(
// OGCG-MAYTRAP: call double @frexp(double noundef
// OGCG-MAYTRAP: call float @frexpf(float noundef
// OGCG-MAYTRAP: call x86_fp80 @frexpl(x86_fp80 noundef
// OGCG-MAYTRAP-LABEL: define {{.*}} @test_modf_family(
// OGCG-MAYTRAP: call double @modf(double noundef
// OGCG-MAYTRAP: call float @modff(float noundef
// OGCG-MAYTRAP: call x86_fp80 @modfl(x86_fp80 noundef
// OGCG-MAYTRAP-LABEL: define {{.*}} @test_sincos_family(
// OGCG-MAYTRAP: call void @sincos(double noundef
// OGCG-MAYTRAP: call void @sincosf(float noundef
// OGCG-MAYTRAP: call void @sincosl(x86_fp80 noundef
