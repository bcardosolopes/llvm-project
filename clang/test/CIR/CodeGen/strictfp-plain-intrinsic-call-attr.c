// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -ffp-exception-behavior=strict -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o - | FileCheck %s

float test_fabsf(float x) {
  // CHECK-LABEL: define{{.*}} @test_fabsf(
  // CHECK: call float @llvm.fabs.f32(float %{{.*}}) #[[STRICT:[0-9]+]]
  return __builtin_fabsf(x);
}

double test_copysign(double x, double y) {
  // CHECK-LABEL: define{{.*}} @test_copysign(
  // CHECK: call double @llvm.copysign.f64(double %{{.*}}, double %{{.*}}) #[[STRICT]]
  return __builtin_copysign(x, y);
}

float test_fmaximum_num(float x, float y) {
  // CHECK-LABEL: define{{.*}} @test_fmaximum_num(
  // CHECK: call double @llvm.maximumnum.f64(double %{{.*}}, double %{{.*}}) #[[STRICT]]
  return __builtin_fmaximum_num(x, y);
}

float test_fminimum_num(float x, float y) {
  // CHECK-LABEL: define{{.*}} @test_fminimum_num(
  // CHECK: call double @llvm.minimumnum.f64(double %{{.*}}, double %{{.*}}) #[[STRICT]]
  return __builtin_fminimum_num(x, y);
}

float test_elementwise_maximumnum(float x, float y) {
  // CHECK-LABEL: define{{.*}} @test_elementwise_maximumnum(
  // CHECK: call float @llvm.maximumnum.f32(float %{{.*}}, float %{{.*}}) #[[STRICT]]
  return __builtin_elementwise_maximumnum(x, y);
}

float test_elementwise_minimumnum(float x, float y) {
  // CHECK-LABEL: define{{.*}} @test_elementwise_minimumnum(
  // CHECK: call float @llvm.minimumnum.f32(float %{{.*}}, float %{{.*}}) #[[STRICT]]
  return __builtin_elementwise_minimumnum(x, y);
}

// CHECK: attributes #[[STRICT]] = { strictfp }
