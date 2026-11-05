// RUN: %clang_cc1 -triple aarch64-linux-gnu -O1 -ffp-exception-behavior=strict -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -O1 -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o - | FileCheck %s

int test_isnan(float x) {
  // CHECK-LABEL: define{{.*}} @test_isnan(
  // CHECK: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 3) #[[STRICT:[0-9]+]]
  // CHECK: zext i1 %{{.*}} to i32
  return __builtin_isnan(x);
}

int test_isinf(float x) {
  // CHECK-LABEL: define{{.*}} @test_isinf(
  // CHECK: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 516) #[[STRICT]]
  // CHECK: zext i1 %{{.*}} to i32
  return __builtin_isinf(x);
}

int test_isfinite(float x) {
  // CHECK-LABEL: define{{.*}} @test_isfinite(
  // CHECK: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 504) #[[STRICT]]
  // CHECK: zext i1 %{{.*}} to i32
  return __builtin_isfinite(x);
}

int test_isnormal(float x) {
  // CHECK-LABEL: define{{.*}} @test_isnormal(
  // CHECK: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 264) #[[STRICT]]
  // CHECK: zext i1 %{{.*}} to i32
  return __builtin_isnormal(x);
}

int test_isfpclass(float x) {
  // CHECK-LABEL: define{{.*}} @test_isfpclass(
  // CHECK: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 96) #[[STRICT]]
  // CHECK: zext i1 %{{.*}} to i32
  return __builtin_isfpclass(x, 96);
}

// CHECK: attributes #[[STRICT]] = { strictfp }
