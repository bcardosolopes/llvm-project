// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm \
// RUN:   -disable-llvm-passes -ffp-exception-behavior=strict -fclangir %s -o - | FileCheck %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm \
// RUN:   -disable-llvm-passes -ffp-exception-behavior=strict %s -o - | FileCheck %s

float test_modff(float x, float *ip) {
  return __builtin_modff(x, ip);
}

double test_modf(double x, double *ip) {
  return __builtin_modf(x, ip);
}

// CHECK-LABEL: define {{.*}} @test_modff(
// CHECK: call float @modff(float noundef %{{.*}}, ptr noundef %{{.*}}) #[[CALL_ATTR:[0-9]+]]
// CHECK: ret float
// CHECK: declare float @modff(float noundef, ptr noundef) #[[DECL_ATTR:[0-9]+]]

// CHECK-LABEL: define {{.*}} @test_modf(
// CHECK: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}}) #[[CALL_ATTR]]
// CHECK: ret double
// CHECK: declare double @modf(double noundef, ptr noundef) #[[DECL_ATTR]]

// CHECK: attributes #[[DECL_ATTR]] = { nounwind{{.*}} }
// CHECK: attributes #[[CALL_ATTR]] = { nounwind strictfp }
