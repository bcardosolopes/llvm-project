// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -frounding-math -ffp-exception-behavior=strict -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -frounding-math -ffp-exception-behavior=strict -O2 -fclangir -emit-llvm -o - %s | FileCheck %s %}

typedef float float4 __attribute__((ext_vector_type(4)));

// CHECK-LABEL: define dso_local {{.*}}@ceil4(
// CHECK-SAME: <4 x float> noundef [[A:%.*]])
// CHECK: [[RET:%.*]] = tail call <4 x float> @llvm.experimental.constrained.ceil.v4f32(<4 x float> [[A]], metadata !"fpexcept.strict")
// CHECK: ret <4 x float> [[RET]]
float4 ceil4(float4 a) { return __builtin_elementwise_ceil(a); }

// CHECK-LABEL: define dso_local {{.*}}@abs4(
// CHECK-SAME: <4 x float> noundef [[A:%.*]])
// CHECK: [[RET:%.*]] = tail call <4 x float> @llvm.fabs.v4f32(<4 x float> [[A]])
// CHECK: ret <4 x float> [[RET]]
float4 abs4(float4 a) { return __builtin_elementwise_abs(a); }

// CHECK-LABEL: define dso_local {{.*}}@sin4(
// CHECK-SAME: <4 x float> noundef [[A:%.*]])
// CHECK: [[RET:%.*]] = tail call <4 x float> @llvm.experimental.constrained.sin.v4f32(<4 x float> [[A]], metadata !"round.dynamic", metadata !"fpexcept.strict")
// CHECK: ret <4 x float> [[RET]]
float4 sin4(float4 a) { return __builtin_elementwise_sin(a); }

// CHECK-LABEL: define dso_local {{.*}}@maxnum4(
// CHECK-SAME: <4 x float> noundef [[A:%.*]], <4 x float> noundef [[B:%.*]])
// CHECK: [[RET:%.*]] = tail call <4 x float> @llvm.maxnum.v4f32(<4 x float> [[A]], <4 x float> [[B]])
// CHECK: ret <4 x float> [[RET]]
float4 maxnum4(float4 a, float4 b) {
  return __builtin_elementwise_maxnum(a, b);
}

// CHECK-LABEL: define dso_local {{.*}}@minnum4(
// CHECK-SAME: <4 x float> noundef [[A:%.*]], <4 x float> noundef [[B:%.*]])
// CHECK: [[RET:%.*]] = tail call <4 x float> @llvm.minnum.v4f32(<4 x float> [[A]], <4 x float> [[B]])
// CHECK: ret <4 x float> [[RET]]
float4 minnum4(float4 a, float4 b) {
  return __builtin_elementwise_minnum(a, b);
}

// CHECK-LABEL: define dso_local {{.*}}@fma4(
// CHECK-SAME: <4 x float> noundef [[A:%.*]], <4 x float> noundef [[B:%.*]], <4 x float> noundef [[C:%.*]])
// CHECK: [[RET:%.*]] = tail call <4 x float> @llvm.experimental.constrained.fma.v4f32(<4 x float> [[A]], <4 x float> [[B]], <4 x float> [[C]], metadata !"round.dynamic", metadata !"fpexcept.strict")
// CHECK: ret <4 x float> [[RET]]
float4 fma4(float4 a, float4 b, float4 c) {
  return __builtin_elementwise_fma(a, b, c);
}
