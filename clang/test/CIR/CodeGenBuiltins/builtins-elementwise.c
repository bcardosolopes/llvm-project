// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

typedef int vint4 __attribute__((ext_vector_type(4)));
typedef float vfloat4 __attribute__((ext_vector_type(4)));
typedef double vdouble4 __attribute__((ext_vector_type(4)));

void test_builtin_elementwise_acos(float f, double d, vfloat4 vf4,
                                   vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_acos
  // LLVM-LABEL: test_builtin_elementwise_acos
  // OGCG-LABEL: test_builtin_elementwise_acos

  // CIR: %{{.*}} = cir.acos %{{.*}} : !cir.float
  // LLVM: %{{.*}} = call float @llvm.acos.f32(float %{{.*}})
  // OGCG: %{{.*}} = call float @llvm.acos.f32(float %{{.*}})
  f = __builtin_elementwise_acos(f);

  // CIR: %{{.*}} = cir.acos %{{.*}} : !cir.double
  // LLVM: %{{.*}} = call double @llvm.acos.f64(double %{{.*}})
  // OGCG: %{{.*}} = call double @llvm.acos.f64(double %{{.*}})
  d = __builtin_elementwise_acos(d);

  // CIR: %{{.*}} = cir.acos %{{.*}} : !cir.vector<4 x !cir.float>
  // LLVM: %{{.*}} = call <4 x float> @llvm.acos.v4f32(<4 x float> %{{.*}})
  // OGCG: %{{.*}} = call <4 x float> @llvm.acos.v4f32(<4 x float> %{{.*}})
  vf4 = __builtin_elementwise_acos(vf4);

  // CIR: %{{.*}} = cir.acos %{{.*}} : !cir.vector<4 x !cir.double>
  // LLVM: %{{.*}} = call <4 x double> @llvm.acos.v4f64(<4 x double> %{{.*}})
  // OGCG: %{{.*}} = call <4 x double> @llvm.acos.v4f64(<4 x double> %{{.*}})
  vd4 = __builtin_elementwise_acos(vd4);
}

void test_builtin_elementwise_asin(float f, double d, vfloat4 vf4,
  vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_asin
  // LLVM-LABEL: test_builtin_elementwise_asin
  // OGCG-LABEL: test_builtin_elementwise_asin

  // CIR: %{{.*}} = cir.asin %{{.*}} : !cir.float
  // LLVM: %{{.*}} = call float @llvm.asin.f32(float %{{.*}})
  // OGCG: %{{.*}} = call float @llvm.asin.f32(float %{{.*}})
  f = __builtin_elementwise_asin(f);

  // CIR: %{{.*}} = cir.asin %{{.*}} : !cir.double
  // LLVM: %{{.*}} = call double @llvm.asin.f64(double %{{.*}})
  // OGCG: %{{.*}} = call double @llvm.asin.f64(double %{{.*}})
  d = __builtin_elementwise_asin(d);

  // CIR: %{{.*}} = cir.asin %{{.*}} : !cir.vector<4 x !cir.float>
  // LLVM: %{{.*}} = call <4 x float> @llvm.asin.v4f32(<4 x float> %{{.*}})
  // OGCG: %{{.*}} = call <4 x float> @llvm.asin.v4f32(<4 x float> %{{.*}})
  vf4 = __builtin_elementwise_asin(vf4);

  // CIR: %{{.*}} = cir.asin %{{.*}} : !cir.vector<4 x !cir.double>
  // LLVM: %{{.*}} = call <4 x double> @llvm.asin.v4f64(<4 x double> %{{.*}})
  // OGCG: %{{.*}} = call <4 x double> @llvm.asin.v4f64(<4 x double> %{{.*}})
  vd4 = __builtin_elementwise_asin(vd4);
}

void test_builtin_elementwise_atan(float f, double d, vfloat4 vf4,
  vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_atan
  // LLVM-LABEL: test_builtin_elementwise_atan
  // OGCG-LABEL: test_builtin_elementwise_atan

  // CIR: %{{.*}} = cir.atan %{{.*}} : !cir.float
  // LLVM: %{{.*}} = call float @llvm.atan.f32(float %{{.*}})
  // OGCG: %{{.*}} = call float @llvm.atan.f32(float %{{.*}})
  f = __builtin_elementwise_atan(f);

  // CIR: %{{.*}} = cir.atan %{{.*}} : !cir.double
  // LLVM: %{{.*}} = call double @llvm.atan.f64(double %{{.*}})
  // OGCG: %{{.*}} = call double @llvm.atan.f64(double %{{.*}})
  d = __builtin_elementwise_atan(d);

  // CIR: %{{.*}} = cir.atan %{{.*}} : !cir.vector<4 x !cir.float>
  // LLVM: %{{.*}} = call <4 x float> @llvm.atan.v4f32(<4 x float> %{{.*}})
  // OGCG: %{{.*}} = call <4 x float> @llvm.atan.v4f32(<4 x float> %{{.*}})
  vf4 = __builtin_elementwise_atan(vf4);

  // CIR: %{{.*}} = cir.atan %{{.*}} : !cir.vector<4 x !cir.double>
  // LLVM: %{{.*}} = call <4 x double> @llvm.atan.v4f64(<4 x double> %{{.*}})
  // OGCG: %{{.*}} = call <4 x double> @llvm.atan.v4f64(<4 x double> %{{.*}})
  vd4 = __builtin_elementwise_atan(vd4);
}

void test_builtin_elementwise_cos(float f, double d, vfloat4 vf4,
                                     vdouble4 vd4) {
  // CIR-LABEL: test_builtin_elementwise_cos
  // LLVM-LABEL: test_builtin_elementwise_cos
  // OGCG-LABEL: test_builtin_elementwise_cos

  // CIR: {{%.*}} = cir.cos {{%.*}} : !cir.float
  // LLVM: {{%.*}} = call float @llvm.cos.f32(float {{%.*}})
  // OGCG: {{%.*}} = call float @llvm.cos.f32(float {{%.*}})
  f = __builtin_elementwise_cos(f);

  // CIR: {{%.*}} = cir.cos {{%.*}} : !cir.double
  // LLVM: {{%.*}} = call double @llvm.cos.f64(double {{%.*}})
  // OGCG: {{%.*}} = call double @llvm.cos.f64(double {{%.*}})
  d = __builtin_elementwise_cos(d);

  // CIR: {{%.*}} = cir.cos {{%.*}} : !cir.vector<4 x !cir.float>
  // LLVM: {{%.*}} = call <4 x float> @llvm.cos.v4f32(<4 x float> {{%.*}})
  // OGCG: {{%.*}} = call <4 x float> @llvm.cos.v4f32(<4 x float> {{%.*}})
  vf4 = __builtin_elementwise_cos(vf4);

  // CIR: {{%.*}} = cir.cos {{%.*}} : !cir.vector<4 x !cir.double>
  // LLVM: {{%.*}} = call <4 x double> @llvm.cos.v4f64(<4 x double> {{%.*}})
  // OGCG: {{%.*}} = call <4 x double> @llvm.cos.v4f64(<4 x double> {{%.*}})
  vd4 = __builtin_elementwise_cos(vd4);
}

void test_builtin_elementwise_cosh(float f, double d, vfloat4 vf4,
                                   vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_cosh
  // LLVM-LABEL: test_builtin_elementwise_cosh
  // OGCG-LABEL: test_builtin_elementwise_cosh

  // CIR: {{%.*}} = cir.cosh {{%.*}} : !cir.float
  // LLVM: {{%.*}} = call float @llvm.cosh.f32(float {{%.*}})
  // OGCG: {{%.*}} = call float @llvm.cosh.f32(float {{%.*}})
  f = __builtin_elementwise_cosh(f);

  // CIR: {{%.*}} = cir.cosh {{%.*}} : !cir.double
  // LLVM: {{%.*}} = call double @llvm.cosh.f64(double {{%.*}})
  // OGCG: {{%.*}} = call double @llvm.cosh.f64(double {{%.*}})
  d = __builtin_elementwise_cosh(d);

  // CIR: {{%.*}} = cir.cosh {{%.*}} : !cir.vector<4 x !cir.float>
  // LLVM: {{%.*}} = call <4 x float> @llvm.cosh.v4f32(<4 x float> {{%.*}})
  // OGCG: {{%.*}} = call <4 x float> @llvm.cosh.v4f32(<4 x float> {{%.*}})
  vf4 = __builtin_elementwise_cosh(vf4);

  // CIR: {{%.*}} = cir.cosh {{%.*}} : !cir.vector<4 x !cir.double>
  // LLVM: {{%.*}} = call <4 x double> @llvm.cosh.v4f64(<4 x double> {{%.*}})
  // OGCG: {{%.*}} = call <4 x double> @llvm.cosh.v4f64(<4 x double> {{%.*}})
  vd4 = __builtin_elementwise_cosh(vd4);
}

void test_builtin_elementwise_exp10(float f, double d, vfloat4 vf4,
                                    vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_exp10
  // LLVM-LABEL: test_builtin_elementwise_exp10
  // OGCG-LABEL: test_builtin_elementwise_exp10

  // CIR: {{%.*}} = cir.exp10 {{%.*}} : !cir.float
  // LLVM: {{%.*}} = call float @llvm.exp10.f32(float {{%.*}})
  // OGCG: {{%.*}} = call float @llvm.exp10.f32(float {{%.*}})
  f = __builtin_elementwise_exp10(f);

  // CIR: {{%.*}} = cir.exp10 {{%.*}} : !cir.double
  // LLVM: {{%.*}} = call double @llvm.exp10.f64(double {{%.*}})
  // OGCG: {{%.*}} = call double @llvm.exp10.f64(double {{%.*}})
  d = __builtin_elementwise_exp10(d);

  // CIR: {{%.*}} = cir.exp10 {{%.*}} : !cir.vector<4 x !cir.float>
  // LLVM: {{%.*}} = call <4 x float> @llvm.exp10.v4f32(<4 x float> {{%.*}})
  // OGCG: {{%.*}} = call <4 x float> @llvm.exp10.v4f32(<4 x float> {{%.*}})
  vf4 = __builtin_elementwise_exp10(vf4);

  // CIR: {{%.*}} = cir.exp10 {{%.*}} : !cir.vector<4 x !cir.double>
  // LLVM: {{%.*}} = call <4 x double> @llvm.exp10.v4f64(<4 x double> {{%.*}})
  // OGCG: {{%.*}} = call <4 x double> @llvm.exp10.v4f64(<4 x double> {{%.*}})
  vd4 = __builtin_elementwise_exp10(vd4);
}

void test_builtin_elementwise_sinh(float f, double d, vfloat4 vf4,
                                   vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_sinh
  // LLVM-LABEL: test_builtin_elementwise_sinh
  // OGCG-LABEL: test_builtin_elementwise_sinh

  // CIR: {{%.*}} = cir.sinh {{%.*}} : !cir.float
  // LLVM: {{%.*}} = call float @llvm.sinh.f32(float {{%.*}})
  // OGCG: {{%.*}} = call float @llvm.sinh.f32(float {{%.*}})
  f = __builtin_elementwise_sinh(f);

  // CIR: {{%.*}} = cir.sinh {{%.*}} : !cir.double
  // LLVM: {{%.*}} = call double @llvm.sinh.f64(double {{%.*}})
  // OGCG: {{%.*}} = call double @llvm.sinh.f64(double {{%.*}})
  d = __builtin_elementwise_sinh(d);

  // CIR: {{%.*}} = cir.sinh {{%.*}} : !cir.vector<4 x !cir.float>
  // LLVM: {{%.*}} = call <4 x float> @llvm.sinh.v4f32(<4 x float> {{%.*}})
  // OGCG: {{%.*}} = call <4 x float> @llvm.sinh.v4f32(<4 x float> {{%.*}})
  vf4 = __builtin_elementwise_sinh(vf4);

  // CIR: {{%.*}} = cir.sinh {{%.*}} : !cir.vector<4 x !cir.double>
  // LLVM: {{%.*}} = call <4 x double> @llvm.sinh.v4f64(<4 x double> {{%.*}})
  // OGCG: {{%.*}} = call <4 x double> @llvm.sinh.v4f64(<4 x double> {{%.*}})
  vd4 = __builtin_elementwise_sinh(vd4);
}

void test_builtin_elementwise_tanh(float f, double d, vfloat4 vf4,
                                   vdouble4  vd4) {
  // CIR-LABEL: test_builtin_elementwise_tanh
  // LLVM-LABEL: test_builtin_elementwise_tanh
  // OGCG-LABEL: test_builtin_elementwise_tanh

  // CIR: {{%.*}} = cir.tanh {{%.*}} : !cir.float
  // LLVM: {{%.*}} = call float @llvm.tanh.f32(float {{%.*}})
  // OGCG: {{%.*}} = call float @llvm.tanh.f32(float {{%.*}})
  f = __builtin_elementwise_tanh(f);

  // CIR: {{%.*}} = cir.tanh {{%.*}} : !cir.double
  // LLVM: {{%.*}} = call double @llvm.tanh.f64(double {{%.*}})
  // OGCG: {{%.*}} = call double @llvm.tanh.f64(double {{%.*}})
  d = __builtin_elementwise_tanh(d);

  // CIR: {{%.*}} = cir.tanh {{%.*}} : !cir.vector<4 x !cir.float>
  // LLVM: {{%.*}} = call <4 x float> @llvm.tanh.v4f32(<4 x float> {{%.*}})
  // OGCG: {{%.*}} = call <4 x float> @llvm.tanh.v4f32(<4 x float> {{%.*}})
  vf4 = __builtin_elementwise_tanh(vf4);

  // CIR: {{%.*}} = cir.tanh {{%.*}} : !cir.vector<4 x !cir.double>
  // LLVM: {{%.*}} = call <4 x double> @llvm.tanh.v4f64(<4 x double> {{%.*}})
  // OGCG: {{%.*}} = call <4 x double> @llvm.tanh.v4f64(<4 x double> {{%.*}})
  vd4 = __builtin_elementwise_tanh(vd4);
}
