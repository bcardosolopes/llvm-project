// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -frounding-math -ffp-exception-behavior=strict -emit-llvm -o - %s | FileCheck %s --check-prefix=STRICT-OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -frounding-math -ffp-exception-behavior=strict -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=STRICT-CIR %}

int side_effect_counter(void);

// OGCG-LABEL: define dso_local i32 @test_fpclassify_side_effect_normal(
// OGCG: %iszero = fcmp oeq double
// OGCG-NOT: call i32 @side_effect_counter()
// OGCG: br i1 %iszero, label %fpclassify_end, label %fpclassify_not_zero
// OGCG: fpclassify_not_zero:
// OGCG: %cmp = fcmp uno double
// OGCG-NOT: call i32 @side_effect_counter()
// OGCG: br i1 %cmp, label %fpclassify_end, label %fpclassify_not_nan
// OGCG: fpclassify_not_nan:
// OGCG: %isinf = fcmp oeq double
// OGCG-NOT: call i32 @side_effect_counter()
// OGCG: br i1 %isinf, label %fpclassify_end, label %fpclassify_not_inf
// OGCG: fpclassify_not_inf:
// OGCG: %isnormal = fcmp uge double
// OGCG-NEXT: %call = call i32 @side_effect_counter()
//
// CIR-LABEL: define dso_local i32 @test_fpclassify_side_effect_normal(
// CIR: call i1 @llvm.is.fpclass.f64(double %{{.*}}, i32 3)
// CIR: call i1 @llvm.is.fpclass.f64(double %{{.*}}, i32 516)
// CIR: call i1 @llvm.is.fpclass.f64(double %{{.*}}, i32 264)
// CIR: call i1 @llvm.is.fpclass.f64(double %{{.*}}, i32 96)
// CIR-NOT: call i32 @side_effect_counter()
// CIR: br i1 %{{.*}}, label %[[ZERO:[0-9]+]], label %[[NOT_ZERO:[0-9]+]]
// CIR: [[NOT_ZERO]]:
// CIR-NOT: call i32 @side_effect_counter()
// CIR: br i1 %{{.*}}, label %[[NAN:[0-9]+]], label %[[NOT_NAN:[0-9]+]]
// CIR: [[NOT_NAN]]:
// CIR-NOT: call i32 @side_effect_counter()
// CIR: br i1 %{{.*}}, label %[[INF:[0-9]+]], label %[[NOT_INF:[0-9]+]]
// CIR: [[NOT_INF]]:
// CIR-NEXT: br i1 %{{.*}}, label %[[NORMAL:[0-9]+]], label %[[SUBNORMAL:[0-9]+]]
// CIR: [[NORMAL]]:
// CIR-NEXT: %{{.*}} = call i32 @side_effect_counter()
//
// STRICT-OGCG-LABEL: define dso_local i32 @test_fpclassify_side_effect_normal(
// STRICT-OGCG: %iszero = call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-OGCG-NOT: call i32 @side_effect_counter()
// STRICT-OGCG: br i1 %iszero, label %fpclassify_end, label %fpclassify_not_zero
// STRICT-OGCG: fpclassify_not_zero:
// STRICT-OGCG: %cmp = call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-OGCG-NOT: call i32 @side_effect_counter()
// STRICT-OGCG: br i1 %cmp, label %fpclassify_end, label %fpclassify_not_nan
// STRICT-OGCG: fpclassify_not_nan:
// STRICT-OGCG: %isinf = call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-OGCG-NOT: call i32 @side_effect_counter()
// STRICT-OGCG: br i1 %isinf, label %fpclassify_end, label %fpclassify_not_inf
// STRICT-OGCG: fpclassify_not_inf:
// STRICT-OGCG: %isnormal = call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-OGCG-NEXT: %call = call i32 @side_effect_counter()
//
// STRICT-CIR-LABEL: define dso_local i32 @test_fpclassify_side_effect_normal(
// STRICT-CIR: call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-CIR-NOT: call i32 @side_effect_counter()
// STRICT-CIR: br i1 %{{.*}}, label %[[ZERO:[0-9]+]], label %[[NOT_ZERO:[0-9]+]]
// STRICT-CIR: [[NOT_ZERO]]:
// STRICT-CIR: call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-CIR-NOT: call i32 @side_effect_counter()
// STRICT-CIR: br i1 %{{.*}}, label %[[NAN:[0-9]+]], label %[[NOT_NAN:[0-9]+]]
// STRICT-CIR: [[NOT_NAN]]:
// STRICT-CIR: call double @llvm.fabs.f64(double
// STRICT-CIR: call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-CIR-NOT: call i32 @side_effect_counter()
// STRICT-CIR: br i1 %{{.*}}, label %[[INF:[0-9]+]], label %[[NOT_INF:[0-9]+]]
// STRICT-CIR: [[NOT_INF]]:
// STRICT-CIR: call i1 @llvm.experimental.constrained.fcmp.f64(double
// STRICT-CIR-NEXT: br i1 %{{.*}}, label %[[NORMAL:[0-9]+]], label %[[SUBNORMAL:[0-9]+]]
// STRICT-CIR: [[NORMAL]]:
// STRICT-CIR-NEXT: %{{.*}} = call i32 @side_effect_counter()
int test_fpclassify_side_effect_normal(double x) {
  return __builtin_fpclassify(0, 1, side_effect_counter(), 3, 4, x);
}
