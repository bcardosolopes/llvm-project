// RUN: %clang_cc1 -triple arm64-apple-ios7.0 -target-abi darwinpcs -target-feature +sve -target-feature +sve2p1 -mvscale-min=1 -mvscale-max=1 -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple arm64-apple-ios7.0 -target-abi darwinpcs -target-feature +sve -target-feature +sve2p1 -mvscale-min=1 -mvscale-max=1 -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK

// REQUIRES: aarch64-registered-target

#include <arm_sve.h>

void test_tuple_reg_count(svfloat32_t x, svfloat32x2_t y) {
  void tuple_reg_count_callee(svfloat32_t, svfloat32_t, svfloat32_t,
                              svfloat32_t, svfloat32_t, svfloat32_t,
                              svfloat32_t, svfloat32x2_t);
  tuple_reg_count_callee(x, x, x, x, x, x, x, y);
}

// CHECK-LABEL: define{{.*}} @test_tuple_reg_count(
// CHECK: call void @tuple_reg_count_callee(<vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}})
// CHECK: declare void @tuple_reg_count_callee(<vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>)

void test_tuple_reg_count_bool(svboolx4_t x, svboolx4_t y) {
  void tuple_reg_count_bool_callee(svboolx4_t, svboolx4_t);
  tuple_reg_count_bool_callee(x, y);
}

// CHECK-LABEL: define{{.*}} @test_tuple_reg_count_bool(
// CHECK: call void @tuple_reg_count_bool_callee(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}})
// CHECK: declare void @tuple_reg_count_bool_callee(<vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>)

svboolx4_t ret_boolx4(svboolx4_t x) {
  return x;
}

// CHECK-LABEL: define{{.*}} @ret_boolx4(
// CHECK-SAME: <vscale x 16 x i1>{{[^%]*}}%{{.*}}, <vscale x 16 x i1>{{[^%]*}}%{{.*}}, <vscale x 16 x i1>{{[^%]*}}%{{.*}}, <vscale x 16 x i1>{{[^%]*}}%{{.*}})
// CHECK: %[[BOOL0:.*]] = insertvalue {{.*}} {{(undef|poison)}}, <vscale x 16 x i1> %{{.*}}, 0
// CHECK: %[[BOOL1:.*]] = insertvalue {{.*}} %[[BOOL0]], <vscale x 16 x i1> %{{.*}}, 1
// CHECK: %[[BOOL2:.*]] = insertvalue {{.*}} %[[BOOL1]], <vscale x 16 x i1> %{{.*}}, 2
// CHECK: %[[BOOL3:.*]] = insertvalue {{.*}} %[[BOOL2]], <vscale x 16 x i1> %{{.*}}, 3
// CHECK: ret {{.*}}<vscale x 16 x i1>{{.*}} %{{.*}}

svfloat64x4_t ret_f64x4(svfloat64x4_t x) {
  return x;
}

// CHECK-LABEL: define{{.*}} @ret_f64x4(
// CHECK-SAME: <vscale x 2 x double>{{[^%]*}}%{{.*}}, <vscale x 2 x double>{{[^%]*}}%{{.*}}, <vscale x 2 x double>{{[^%]*}}%{{.*}}, <vscale x 2 x double>{{[^%]*}}%{{.*}})
// CHECK: %[[F640:.*]] = insertvalue {{.*}} {{(undef|poison)}}, <vscale x 2 x double> %{{.*}}, 0
// CHECK: %[[F641:.*]] = insertvalue {{.*}} %[[F640]], <vscale x 2 x double> %{{.*}}, 1
// CHECK: %[[F642:.*]] = insertvalue {{.*}} %[[F641]], <vscale x 2 x double> %{{.*}}, 2
// CHECK: %[[F643:.*]] = insertvalue {{.*}} %[[F642]], <vscale x 2 x double> %{{.*}}, 3
// CHECK: ret {{.*}}<vscale x 2 x double>{{.*}} %{{.*}}
