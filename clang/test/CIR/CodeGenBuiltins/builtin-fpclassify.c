// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// CIR-LABEL: @test_fpclassify_double
// CIR:   %[[V:.*]] = cir.load {{.*}} : !cir.ptr<!cir.double>, !cir.double
// CIR:   %[[NAN:.*]] = cir.is_fp_class %[[V]], 3 : (!cir.double) -> !cir.bool
// CIR:   %[[INF:.*]] = cir.is_fp_class %[[V]], 516 : (!cir.double) -> !cir.bool
// CIR:   %[[NORMAL:.*]] = cir.is_fp_class %[[V]], 264 : (!cir.double) -> !cir.bool
// CIR:   %[[ZERO:.*]] = cir.is_fp_class %[[V]], 96 : (!cir.double) -> !cir.bool
// CIR:   %[[S1:.*]] = cir.select if %[[ZERO]] then {{.*}} else {{.*}}
// CIR:   %[[S2:.*]] = cir.select if %[[NORMAL]] then {{.*}} else %[[S1]]
// CIR:   %[[S3:.*]] = cir.select if %[[INF]] then {{.*}} else %[[S2]]
// CIR:   %[[S4:.*]] = cir.select if %[[NAN]] then {{.*}} else %[[S3]]

// LLVM-LABEL: @test_fpclassify_double
// LLVM:   %[[V:.*]] = load double
// LLVM:   %[[NAN:.*]] = call i1 @llvm.is.fpclass.f64(double %[[V]], i32 3)
// LLVM:   %[[INF:.*]] = call i1 @llvm.is.fpclass.f64(double %[[V]], i32 516)
// LLVM:   %[[NORMAL:.*]] = call i1 @llvm.is.fpclass.f64(double %[[V]], i32 264)
// LLVM:   %[[ZERO:.*]] = call i1 @llvm.is.fpclass.f64(double %[[V]], i32 96)
// LLVM:   %[[S1:.*]] = select i1 %[[ZERO]], i32 4, i32 3
// LLVM:   %[[S2:.*]] = select i1 %[[NORMAL]], i32 2, i32 %[[S1]]
// LLVM:   %[[S3:.*]] = select i1 %[[INF]], i32 1, i32 %[[S2]]
// LLVM:   %[[S4:.*]] = select i1 %[[NAN]], i32 0, i32 %[[S3]]

// OGCG-LABEL: @test_fpclassify_double
// OGCG:   %[[ISZERO:.*]] = fcmp oeq double %{{.*}}, 0.000000e+00
// OGCG:   br i1 %[[ISZERO]]
int test_fpclassify_double(double x) {
  return __builtin_fpclassify(0, 1, 2, 3, 4, x);
}

// CIR-LABEL: @test_fpclassify_float
// CIR:   cir.is_fp_class %{{.*}}, 3 : (!cir.float) -> !cir.bool
// CIR:   cir.is_fp_class %{{.*}}, 516 : (!cir.float) -> !cir.bool
// CIR:   cir.is_fp_class %{{.*}}, 264 : (!cir.float) -> !cir.bool
// CIR:   cir.is_fp_class %{{.*}}, 96 : (!cir.float) -> !cir.bool
// CIR:   cir.select
// CIR:   cir.select
// CIR:   cir.select
// CIR:   cir.select

// LLVM-LABEL: @test_fpclassify_float
// LLVM:   call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 3)
// LLVM:   call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 516)
// LLVM:   call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 264)
// LLVM:   call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 96)
// LLVM:   select i1 %{{.*}}, i32 4, i32 3
// LLVM:   select i1 %{{.*}}, i32 2
// LLVM:   select i1 %{{.*}}, i32 1
// LLVM:   select i1 %{{.*}}, i32 0

// OGCG-LABEL: @test_fpclassify_float
// OGCG:   fcmp oeq float %{{.*}}, 0.000000e+00
int test_fpclassify_float(float x) {
  return __builtin_fpclassify(0, 1, 2, 3, 4, x);
}

// CIR-LABEL: @test_fpclassify_long_double
// CIR:   cir.is_fp_class %{{.*}}, 3 : (!cir.long_double<!cir.f80>) -> !cir.bool

// LLVM-LABEL: @test_fpclassify_long_double
// LLVM:   call i1 @llvm.is.fpclass.f80(x86_fp80 %{{.*}}, i32 3)

// OGCG-LABEL: @test_fpclassify_long_double
// OGCG:   fcmp oeq x86_fp80
int test_fpclassify_long_double(long double x) {
  return __builtin_fpclassify(0, 1, 2, 3, 4, x);
}

// Test with non-trivial classification values
// CIR-LABEL: @test_fpclassify_custom_values
// CIR:   %[[NAN_VAL:.*]] = cir.const #cir.int<10>
// CIR:   %[[INF_VAL:.*]] = cir.const #cir.int<20>
// CIR:   %[[NORMAL_VAL:.*]] = cir.const #cir.int<30>
// CIR:   %[[SUBNORMAL_VAL:.*]] = cir.const #cir.int<40>
// CIR:   %[[ZERO_VAL:.*]] = cir.const #cir.int<50>

// LLVM-LABEL: @test_fpclassify_custom_values
// LLVM:   select i1 %{{.*}}, i32 50, i32 40
// LLVM:   select i1 %{{.*}}, i32 30
// LLVM:   select i1 %{{.*}}, i32 20
// LLVM:   select i1 %{{.*}}, i32 10
int test_fpclassify_custom_values(double x) {
  return __builtin_fpclassify(10, 20, 30, 40, 50, x);
}

// Discarded-result coverage for compile-time/path parity audit.
// In ignored-result contexts, CIR evaluates the FP operand for side effects
// but does not materialize fpclassify classification/select result chains.
//
// CIR-LABEL: @test_fpclassify_discarded
// CIR-NOT:   cir.is_fp_class
// CIR-NOT:   cir.select
// CIR:       cir.return
//
// LLVM-LABEL: @test_fpclassify_discarded
// LLVM-NOT:   @llvm.is.fpclass.f32
// LLVM-NOT:   select i1
// LLVM:       ret void
//
// OGCG-LABEL: @test_fpclassify_discarded
// OGCG:       fcmp oeq float
// OGCG:       br i1
void test_fpclassify_discarded(float x) {
  (void)__builtin_fpclassify(0, 1, 2, 3, 4, x);
}

int side_effect_counter(void);

// Ensure ignored-result fast path still preserves side effects from class-value
// arguments.
//
// CIR-LABEL: @test_fpclassify_discarded_side_effects
// CIR:       cir.call @side_effect_counter
// CIR-NOT:   cir.is_fp_class
// CIR-NOT:   cir.select
// CIR:       cir.return
//
// LLVM-LABEL: @test_fpclassify_discarded_side_effects
// LLVM:       call i32 @side_effect_counter()
// LLVM-NOT:   @llvm.is.fpclass.f32
// LLVM-NOT:   select i1
// LLVM:       ret void
//
// OGCG-LABEL: @test_fpclassify_discarded_side_effects
// OGCG:       call i32 @side_effect_counter()
void test_fpclassify_discarded_side_effects(float x) {
  (void)__builtin_fpclassify(side_effect_counter(), 1, 2, 3, 4, x);
}

// Discarded __builtin_isinf_sign should evaluate argument side effects but
// avoid materializing classify/sign/select result chains.
//
// CIR-LABEL: @test_isinf_sign_discarded
// CIR:       cir.call @side_effect_float
// CIR-NOT:   cir.fabs
// CIR-NOT:   cir.is_fp_class
// CIR-NOT:   cir.signbit
// CIR-NOT:   cir.select
// CIR:       cir.return
//
// LLVM-LABEL: @test_isinf_sign_discarded
// LLVM:       call float @side_effect_float()
// LLVM-NOT:   @llvm.fabs.f32
// LLVM-NOT:   @llvm.is.fpclass.f32
// LLVM-NOT:   icmp slt i32
// LLVM-NOT:   select i1
// LLVM:       ret void
float side_effect_float(void);
void test_isinf_sign_discarded(void) {
  (void)__builtin_isinf_sign(side_effect_float());
}

// Discarded-result fast paths for scalar FP predicate builtins should keep
// argument side effects and skip predicate materialization.
//
// CIR-LABEL: @test_fp_predicates_discarded
// CIR:       cir.call @side_effect_float
// CIR:       cir.call @side_effect_float
// CIR:       cir.call @side_effect_float
// CIR:       cir.call @side_effect_float
// CIR:       cir.call @side_effect_float
// CIR-NOT:   cir.is_fp_class
// CIR-NOT:   cir.cmp
// CIR-NOT:   cir.cast(bool_to_int
// CIR-NOT:   cir.signbit
// CIR-NOT:   cir.select
// CIR:       cir.return
//
// LLVM-LABEL: @test_fp_predicates_discarded
// LLVM:       call float @side_effect_float()
// LLVM:       call float @side_effect_float()
// LLVM:       call float @side_effect_float()
// LLVM:       call float @side_effect_float()
// LLVM:       call float @side_effect_float()
// LLVM-NOT:   @llvm.is.fpclass.f32
// LLVM-NOT:   fcmp
// LLVM-NOT:   icmp slt i32
// LLVM-NOT:   select i1
// LLVM:       ret void
void test_fp_predicates_discarded(void) {
  (void)__builtin_isfinite(side_effect_float());
  (void)__builtin_isnormal(side_effect_float());
  (void)__builtin_signbit(side_effect_float());
  (void)__builtin_isunordered(side_effect_float(), side_effect_float());
}
