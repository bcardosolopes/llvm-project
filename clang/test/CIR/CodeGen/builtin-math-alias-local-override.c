// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Local definitions that intentionally shadow libm names. These must remain
// direct calls and not be rewritten to LLVM math intrinsics.
static double modf(double x, double *iptr) {
  *iptr = x;
  return 1.0;
}

static float modff(float x, float *iptr) {
  *iptr = x;
  return 2.0f;
}

static long double modfl(long double x, long double *iptr) {
  *iptr = x;
  return 3.0L;
}

static void sincos(double x, double *sinp, double *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void sincosf(float x, float *sinp, float *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void sincosl(long double x, long double *sinp, long double *cosp) {
  *sinp = x;
  *cosp = -x;
}

double test_local_modf_alias(double x, double *iptr) {
  return modf(x, iptr);
}

// CIR-LABEL: @test_local_modf_alias
// CIR: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_local_modf_alias
// OGCG: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

float test_local_modff_alias(float x, float *iptr) {
  return modff(x, iptr);
}

// CIR-LABEL: @test_local_modff_alias
// CIR: call float @modff(float noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_local_modff_alias
// OGCG: call float @modff(float noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

long double test_local_modfl_alias(long double x, long double *iptr) {
  return modfl(x, iptr);
}

// CIR-LABEL: @test_local_modfl_alias
// CIR: call x86_fp80 @modfl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_local_modfl_alias
// OGCG: call x86_fp80 @modfl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

void test_local_sincos_alias(double x, double *sinp, double *cosp) {
  sincos(x, sinp, cosp);
}

// CIR-LABEL: @test_local_sincos_alias
// CIR: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_local_sincos_alias
// OGCG: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

void test_local_sincosf_alias(float x, float *sinp, float *cosp) {
  sincosf(x, sinp, cosp);
}

// CIR-LABEL: @test_local_sincosf_alias
// CIR: call void @sincosf(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_local_sincosf_alias
// OGCG: call void @sincosf(float noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

void test_local_sincosl_alias(long double x, long double *sinp,
                              long double *cosp) {
  sincosl(x, sinp, cosp);
}

// CIR-LABEL: @test_local_sincosl_alias
// CIR: call void @sincosl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_local_sincosl_alias
// OGCG: call void @sincosl(x86_fp80 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.
