// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef float __v4sf __attribute__((__vector_size__(16)));
typedef double __v2df __attribute__((__vector_size__(16)));

#define CUR_DIR 4

// sqrtss with CUR_DIRECTION rounding and mask
__v4sf test_sqrtss_round_mask(__v4sf a, __v4sf b, __v4sf c, unsigned char m) {
  return __builtin_ia32_sqrtss_round_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_sqrtss_round_mask
// CHECK: extractelement <4 x float> %{{.*}}, i64 0
// CHECK: call float @llvm.sqrt.f32(float
// CHECK: extractelement <4 x float> %{{.*}}, i64 0
// CHECK: select i1 %{{.*}}, float %{{.*}}, float %{{.*}}
// CHECK: insertelement <4 x float> %{{.*}}, float %{{.*}}, i64 0
// OGCG-LABEL: @test_sqrtss_round_mask
// OGCG: extractelement <4 x float> %{{.*}}, i64 0
// OGCG: call float @llvm.sqrt.f32(float
// OGCG: extractelement <4 x float> %{{.*}}, i64 0
// OGCG: select i1 %{{.*}}, float %{{.*}}, float %{{.*}}
// OGCG: insertelement <4 x float> %{{.*}}, float %{{.*}}, i64 0

// sqrtsd with CUR_DIRECTION rounding and mask
__v2df test_sqrtsd_round_mask(__v2df a, __v2df b, __v2df c, unsigned char m) {
  return __builtin_ia32_sqrtsd_round_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_sqrtsd_round_mask
// CHECK: extractelement <2 x double> %{{.*}}, i64 0
// CHECK: call double @llvm.sqrt.f64(double
// CHECK: extractelement <2 x double> %{{.*}}, i64 0
// CHECK: select i1 %{{.*}}, double %{{.*}}, double %{{.*}}
// CHECK: insertelement <2 x double> %{{.*}}, double %{{.*}}, i64 0
// OGCG-LABEL: @test_sqrtsd_round_mask
// OGCG: extractelement <2 x double> %{{.*}}, i64 0
// OGCG: call double @llvm.sqrt.f64(double
// OGCG: extractelement <2 x double> %{{.*}}, i64 0
// OGCG: select i1 %{{.*}}, double %{{.*}}, double %{{.*}}
// OGCG: insertelement <2 x double> %{{.*}}, double %{{.*}}, i64 0

// sqrtss with explicit rounding (non-CUR_DIRECTION) uses x86 intrinsic
__v4sf test_sqrtss_round_mask_rnd(__v4sf a, __v4sf b, __v4sf c, unsigned char m) {
  return __builtin_ia32_sqrtss_round_mask(a, b, c, m, 8);
}
// CHECK-LABEL: @test_sqrtss_round_mask_rnd
// CHECK: call <4 x float> @llvm.x86.avx512.mask.sqrt.ss(
// OGCG-LABEL: @test_sqrtss_round_mask_rnd
// OGCG: call <4 x float> @llvm.x86.avx512.mask.sqrt.ss(

// sqrtsd with explicit rounding (non-CUR_DIRECTION) uses x86 intrinsic
__v2df test_sqrtsd_round_mask_rnd(__v2df a, __v2df b, __v2df c, unsigned char m) {
  return __builtin_ia32_sqrtsd_round_mask(a, b, c, m, 8);
}
// CHECK-LABEL: @test_sqrtsd_round_mask_rnd
// CHECK: call <2 x double> @llvm.x86.avx512.mask.sqrt.sd(
// OGCG-LABEL: @test_sqrtsd_round_mask_rnd
// OGCG: call <2 x double> @llvm.x86.avx512.mask.sqrt.sd(
