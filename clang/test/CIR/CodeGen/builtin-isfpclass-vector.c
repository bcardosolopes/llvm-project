// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s

typedef float float4 __attribute__((ext_vector_type(4)));
typedef double double4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef long long4 __attribute__((ext_vector_type(4)));

// CHECK-LABEL: @check_isfpclass_nan_v4f32(
// CHECK: call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %{{.*}}, i32 3)
// CHECK: zext <4 x i1> %{{.*}} to <4 x i32>
int4 check_isfpclass_nan_v4f32(float4 x) {
  return __builtin_isfpclass(x, 3 /*NaN*/);
}

// CHECK-LABEL: @check_isfpclass_nan_v4f64(
// CHECK: call <4 x i1> @llvm.is.fpclass.v4f64(<4 x double> %{{.*}}, i32 3)
// CHECK: zext <4 x i1> %{{.*}} to <4 x i64>
long4 check_isfpclass_nan_v4f64(double4 x) {
  return __builtin_isfpclass(x, 3 /*NaN*/);
}

// CHECK-LABEL: @check_isfpclass_inf_v4f32(
// CHECK: call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %{{.*}}, i32 516)
int4 check_isfpclass_inf_v4f32(float4 x) {
  return __builtin_isfpclass(x, 516 /*Inf*/);
}
