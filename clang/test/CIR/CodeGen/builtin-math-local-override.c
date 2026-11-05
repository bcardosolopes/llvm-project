// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Local definitions that shadow builtin spellings should stay as plain calls.
static double __builtin_modf(double x, double *iptr) {
  *iptr = x;
  return 1.0;
}

static float __builtin_modff(float x, float *iptr) {
  *iptr = x;
  return 2.0f;
}

static long double __builtin_modfl(long double x, long double *iptr) {
  *iptr = x;
  return 3.0L;
}

static double __builtin_frexp(double x, int *eptr) {
  *eptr = 4;
  return x;
}

static float __builtin_frexpf(float x, int *eptr) {
  *eptr = 5;
  return x;
}

static long double __builtin_frexpl(long double x, int *eptr) {
  *eptr = 6;
  return x;
}

static void __builtin_sincos(double x, double *sinp, double *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void __builtin_sincosf(float x, float *sinp, float *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void __builtin_sincosl(long double x, long double *sinp,
                              long double *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void __builtin_sincospi(double x, double *sinp, double *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void __builtin_sincospif(float x, float *sinp, float *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void __builtin_sincospil(long double x, long double *sinp,
                                long double *cosp) {
  *sinp = x;
  *cosp = -x;
}

double test_local_builtin_modf(double x, double *iptr) {
  return __builtin_modf(x, iptr);
}

// CIR-LABEL: @test_local_builtin_modf
// CIR: call double @__builtin_modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_local_builtin_modf
// OGCG: call double @__builtin_modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

float test_local_builtin_modff(float x, float *iptr) {
  return __builtin_modff(x, iptr);
}

// CIR-LABEL: @test_local_builtin_modff
// CIR: call float @__builtin_modff(float noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_local_builtin_modff
// OGCG: call float @__builtin_modff(float noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

long double test_local_builtin_modfl(long double x, long double *iptr) {
  return __builtin_modfl(x, iptr);
}

// CIR-LABEL: @test_local_builtin_modfl
// CIR: call x86_fp80 @__builtin_modfl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_local_builtin_modfl
// OGCG: call x86_fp80 @__builtin_modfl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

double test_local_builtin_frexp(double x, int *eptr) {
  return __builtin_frexp(x, eptr);
}

// CIR-LABEL: @test_local_builtin_frexp
// CIR: call double @__builtin_frexp(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.frexp.

// OGCG-LABEL: @test_local_builtin_frexp
// OGCG: call double @__builtin_frexp(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.frexp.

float test_local_builtin_frexpf(float x, int *eptr) {
  return __builtin_frexpf(x, eptr);
}

// CIR-LABEL: @test_local_builtin_frexpf
// CIR: call float @__builtin_frexpf(float noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.frexp.

// OGCG-LABEL: @test_local_builtin_frexpf
// OGCG: call float @__builtin_frexpf(float noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.frexp.

long double test_local_builtin_frexpl(long double x, int *eptr) {
  return __builtin_frexpl(x, eptr);
}

// CIR-LABEL: @test_local_builtin_frexpl
// CIR: call x86_fp80 @__builtin_frexpl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.frexp.

// OGCG-LABEL: @test_local_builtin_frexpl
// OGCG: call x86_fp80 @__builtin_frexpl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.frexp.

void test_local_builtin_sincos(double x, double *sinp, double *cosp) {
  __builtin_sincos(x, sinp, cosp);
}

// CIR-LABEL: @test_local_builtin_sincos
// CIR: call void @__builtin_sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_local_builtin_sincos
// OGCG: call void @__builtin_sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

void test_local_builtin_sincosf(float x, float *sinp, float *cosp) {
  __builtin_sincosf(x, sinp, cosp);
}

// CIR-LABEL: @test_local_builtin_sincosf
// CIR: call void @__builtin_sincosf(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_local_builtin_sincosf
// OGCG: call void @__builtin_sincosf(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

void test_local_builtin_sincosl(long double x, long double *sinp,
                                long double *cosp) {
  __builtin_sincosl(x, sinp, cosp);
}

// CIR-LABEL: @test_local_builtin_sincosl
// CIR: call void @__builtin_sincosl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_local_builtin_sincosl
// OGCG: call void @__builtin_sincosl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

void test_local_builtin_sincospi(double x, double *sinp, double *cosp) {
  __builtin_sincospi(x, sinp, cosp);
}

// CIR-LABEL: @test_local_builtin_sincospi
// CIR: call void @__builtin_sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincospi.

// OGCG-LABEL: @test_local_builtin_sincospi
// OGCG: call void @__builtin_sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincospi.

void test_local_builtin_sincospif(float x, float *sinp, float *cosp) {
  __builtin_sincospif(x, sinp, cosp);
}

// CIR-LABEL: @test_local_builtin_sincospif
// CIR: call void @__builtin_sincospif(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincospi.

// OGCG-LABEL: @test_local_builtin_sincospif
// OGCG: call void @__builtin_sincospif(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincospi.

void test_local_builtin_sincospil(long double x, long double *sinp,
                                  long double *cosp) {
  __builtin_sincospil(x, sinp, cosp);
}

// CIR-LABEL: @test_local_builtin_sincospil
// CIR: call void @__builtin_sincospil(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincospi.

// OGCG-LABEL: @test_local_builtin_sincospil
// OGCG: call void @__builtin_sincospil(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincospi.
