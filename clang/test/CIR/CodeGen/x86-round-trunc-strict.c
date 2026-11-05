// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx \
// RUN:   -target-feature +sse4.1 -ffp-exception-behavior=maytrap -DSTRICT=1 \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx \
// RUN:   -target-feature +sse4.1 -ffp-exception-behavior=maytrap -DSTRICT=1 \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

#ifdef STRICT
#pragma float_control(except, on)
#endif

typedef float __v4sf __attribute__((__vector_size__(16)));
typedef float __v8sf __attribute__((__vector_size__(32)));
typedef double __v2df __attribute__((__vector_size__(16)));
typedef double __v4df __attribute__((__vector_size__(32)));

__v4df test_roundpd256_trunc(__v4df x) {
  return __builtin_ia32_roundpd256(x, 0b1011);
}
// LLVM-LABEL: @test_roundpd256_trunc
// LLVM: call <4 x double> @llvm.experimental.constrained.trunc.v4f64(<4 x double> %{{.*}}, metadata !"fpexcept.ignore")
// OGCG-LABEL: @test_roundpd256_trunc
// OGCG: call <4 x double> @llvm.experimental.constrained.trunc.v4f64(<4 x double> %{{.*}}, metadata !"fpexcept.ignore")

__v8sf test_roundps256_trunc(__v8sf x) {
  return __builtin_ia32_roundps256(x, 0b1011);
}
// LLVM-LABEL: @test_roundps256_trunc
// LLVM: call <8 x float> @llvm.experimental.constrained.trunc.v8f32(<8 x float> %{{.*}}, metadata !"fpexcept.ignore")
// OGCG-LABEL: @test_roundps256_trunc
// OGCG: call <8 x float> @llvm.experimental.constrained.trunc.v8f32(<8 x float> %{{.*}}, metadata !"fpexcept.ignore")

__v4sf test_roundss_trunc(__v4sf a, __v4sf b) {
  return __builtin_ia32_roundss(a, b, 0b1011);
}
// LLVM-LABEL: @test_roundss_trunc
// LLVM: call float @llvm.experimental.constrained.trunc.f32(float %{{.*}}, metadata !"fpexcept.ignore")
// OGCG-LABEL: @test_roundss_trunc
// OGCG: call float @llvm.experimental.constrained.trunc.f32(float %{{.*}}, metadata !"fpexcept.ignore")

__v2df test_roundsd_trunc(__v2df a, __v2df b) {
  return __builtin_ia32_roundsd(a, b, 0b1011);
}
// LLVM-LABEL: @test_roundsd_trunc
// LLVM: call double @llvm.experimental.constrained.trunc.f64(double %{{.*}}, metadata !"fpexcept.ignore")
// OGCG-LABEL: @test_roundsd_trunc
// OGCG: call double @llvm.experimental.constrained.trunc.f64(double %{{.*}}, metadata !"fpexcept.ignore")
