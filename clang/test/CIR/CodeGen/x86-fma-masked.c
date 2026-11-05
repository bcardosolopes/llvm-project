// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512fp16 \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512fp16 \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef float __v4sf __attribute__((__vector_size__(16)));
typedef double __v2df __attribute__((__vector_size__(16)));
typedef float __v16sf __attribute__((__vector_size__(64)));
typedef double __v8df __attribute__((__vector_size__(64)));
typedef _Float16 __v32hf __attribute__((__vector_size__(64)));

#define CUR_DIR 4
#define ALT_RND 11

// --- Packed 512-bit FMA ---

// vfmaddps512_mask: fma, passthrough = a
__v16sf test_vfmaddps512_mask(__v16sf a, __v16sf b, __v16sf c, unsigned short m) {
  return __builtin_ia32_vfmaddps512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddps512_mask
// CHECK: call <16 x float> @llvm.fma.v16f32(
// CHECK: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}
// OGCG-LABEL: @test_vfmaddps512_mask
// OGCG: call <16 x float> @llvm.fma.v16f32(
// OGCG: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}

// vfmaddps512_maskz: fma, passthrough = zero
__v16sf test_vfmaddps512_maskz(__v16sf a, __v16sf b, __v16sf c, unsigned short m) {
  return __builtin_ia32_vfmaddps512_maskz(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddps512_maskz
// CHECK: call <16 x float> @llvm.fma.v16f32(
// CHECK: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> zeroinitializer
// OGCG-LABEL: @test_vfmaddps512_maskz
// OGCG: call <16 x float> @llvm.fma.v16f32(
// OGCG: select <16 x i1> %{{.*}}, <16 x float> %{{.*}}, <16 x float> zeroinitializer

// vfmsubps512_mask3: fmsub, passthrough = c (original, not negated)
__v16sf test_vfmsubps512_mask3(__v16sf a, __v16sf b, __v16sf c, unsigned short m) {
  return __builtin_ia32_vfmsubps512_mask3(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmsubps512_mask3
// CHECK: fneg <16 x float>
// CHECK: call <16 x float> @llvm.fma.v16f32(
// CHECK: select <16 x i1>
// OGCG-LABEL: @test_vfmsubps512_mask3
// OGCG: fneg <16 x float>
// OGCG: call <16 x float> @llvm.fma.v16f32(
// OGCG: select <16 x i1>

// vfmaddpd512_mask: double variant
__v8df test_vfmaddpd512_mask(__v8df a, __v8df b, __v8df c, unsigned char m) {
  return __builtin_ia32_vfmaddpd512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddpd512_mask
// CHECK: call <8 x double> @llvm.fma.v8f64(
// CHECK: select <8 x i1>
// OGCG-LABEL: @test_vfmaddpd512_mask
// OGCG: call <8 x double> @llvm.fma.v8f64(
// OGCG: select <8 x i1>

// vfmaddsubps512_mask: addsub is always lowered to target intrinsic
// (including CUR_DIRECTION rounding).
__v16sf test_vfmaddsubps512_mask(__v16sf a, __v16sf b, __v16sf c, unsigned short m) {
  return __builtin_ia32_vfmaddsubps512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddsubps512_mask
// CHECK: call <16 x float> @llvm.x86.avx512.vfmaddsub.ps.512(<16 x float> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}, i32 4)
// CHECK: select <16 x i1>
// OGCG-LABEL: @test_vfmaddsubps512_mask
// OGCG: call <16 x float> @llvm.x86.avx512.vfmaddsub.ps.512(<16 x float> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}, i32 4)
// OGCG: select <16 x i1>

// vfmaddsubpd512_mask: addsub is always lowered to target intrinsic
// (including CUR_DIRECTION rounding).
__v8df test_vfmaddsubpd512_mask(__v8df a, __v8df b, __v8df c, unsigned char m) {
  return __builtin_ia32_vfmaddsubpd512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddsubpd512_mask
// CHECK: call <8 x double> @llvm.x86.avx512.vfmaddsub.pd.512(<8 x double> %{{.*}}, <8 x double> %{{.*}}, <8 x double> %{{.*}}, i32 4)
// CHECK: select <8 x i1>
// OGCG-LABEL: @test_vfmaddsubpd512_mask
// OGCG: call <8 x double> @llvm.x86.avx512.vfmaddsub.pd.512(<8 x double> %{{.*}}, <8 x double> %{{.*}}, <8 x double> %{{.*}}, i32 4)
// OGCG: select <8 x i1>

// vfmaddsubph512_mask: addsub is always lowered to target intrinsic
// (including CUR_DIRECTION rounding).
__v32hf test_vfmaddsubph512_mask(__v32hf a, __v32hf b, __v32hf c, unsigned int m) {
  return __builtin_ia32_vfmaddsubph512_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddsubph512_mask
// CHECK: call <32 x half> @llvm.x86.avx512fp16.vfmaddsub.ph.512(<32 x half> %{{.*}}, <32 x half> %{{.*}}, <32 x half> %{{.*}}, i32 4)
// CHECK: select <32 x i1>
// OGCG-LABEL: @test_vfmaddsubph512_mask
// OGCG: call <32 x half> @llvm.x86.avx512fp16.vfmaddsub.ph.512(<32 x half> %{{.*}}, <32 x half> %{{.*}}, <32 x half> %{{.*}}, i32 4)
// OGCG: select <32 x i1>

// non-default rounding mode uses target intrinsic path.
__v16sf test_vfmaddps512_mask_alt_round(__v16sf a, __v16sf b, __v16sf c, unsigned short m) {
  return __builtin_ia32_vfmaddps512_mask(a, b, c, m, ALT_RND);
}
// CHECK-LABEL: @test_vfmaddps512_mask_alt_round
// CHECK: call <16 x float> @llvm.x86.avx512.vfmadd.ps.512(<16 x float> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}, i32 11)
// CHECK: select <16 x i1>
// OGCG-LABEL: @test_vfmaddps512_mask_alt_round
// OGCG: call <16 x float> @llvm.x86.avx512.vfmadd.ps.512(<16 x float> %{{.*}}, <16 x float> %{{.*}}, <16 x float> %{{.*}}, i32 11)
// OGCG: select <16 x i1>

// --- Scalar FMA ---

// scalar fma with mask: passthrough = a[0]
__v4sf test_vfmaddss3_mask(__v4sf a, __v4sf b, __v4sf c, unsigned char m) {
  return __builtin_ia32_vfmaddss3_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddss3_mask
// CHECK: extractelement <4 x float> %{{.*}}, i64 0
// CHECK: call float @llvm.fma.f32(
// CHECK: select i1
// CHECK: insertelement <4 x float>
// OGCG-LABEL: @test_vfmaddss3_mask
// OGCG: extractelement <4 x float> %{{.*}}, i64 0
// OGCG: call float @llvm.fma.f32(
// OGCG: select i1
// OGCG: insertelement <4 x float>

// scalar fma with maskz: passthrough = zero
__v4sf test_vfmaddss3_maskz(__v4sf a, __v4sf b, __v4sf c, unsigned char m) {
  return __builtin_ia32_vfmaddss3_maskz(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddss3_maskz
// CHECK: call float @llvm.fma.f32(
// CHECK: select i1 %{{.*}}, float %{{.*}}, float 0.000000e+00
// CHECK: insertelement <4 x float>
// OGCG-LABEL: @test_vfmaddss3_maskz
// OGCG: call float @llvm.fma.f32(
// OGCG: select i1 %{{.*}}, float %{{.*}}, float 0.000000e+00
// OGCG: insertelement <4 x float>

// scalar fmsub with mask3: negate c, passthrough = original c[0]
__v4sf test_vfmsubss3_mask3(__v4sf a, __v4sf b, __v4sf c, unsigned char m) {
  return __builtin_ia32_vfmsubss3_mask3(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmsubss3_mask3
// CHECK: fneg <4 x float>
// CHECK: call float @llvm.fma.f32(
// CHECK: select i1
// CHECK: insertelement <4 x float>
// OGCG-LABEL: @test_vfmsubss3_mask3
// OGCG: fneg <4 x float>
// OGCG: call float @llvm.fma.f32(
// OGCG: select i1
// OGCG: insertelement <4 x float>

// scalar double fma
__v2df test_vfmaddsd3_mask(__v2df a, __v2df b, __v2df c, unsigned char m) {
  return __builtin_ia32_vfmaddsd3_mask(a, b, c, m, CUR_DIR);
}
// CHECK-LABEL: @test_vfmaddsd3_mask
// CHECK: call double @llvm.fma.f64(
// CHECK: select i1
// CHECK: insertelement <2 x double>
// OGCG-LABEL: @test_vfmaddsd3_mask
// OGCG: call double @llvm.fma.f64(
// OGCG: select i1
// OGCG: insertelement <2 x double>

// scalar non-default rounding mode uses x86 target intrinsic path.
__v4sf test_vfmaddss3_mask_alt_round(__v4sf a, __v4sf b, __v4sf c, unsigned char m) {
  return __builtin_ia32_vfmaddss3_mask(a, b, c, m, ALT_RND);
}
// CHECK-LABEL: @test_vfmaddss3_mask_alt_round
// CHECK: call float @llvm.x86.avx512.vfmadd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}}, i32 11)
// CHECK: select i1
// CHECK: insertelement <4 x float>
// OGCG-LABEL: @test_vfmaddss3_mask_alt_round
// OGCG: call float @llvm.x86.avx512.vfmadd.f32(float %{{.*}}, float %{{.*}}, float %{{.*}}, i32 11)
// OGCG: select i1
// OGCG: insertelement <4 x float>
