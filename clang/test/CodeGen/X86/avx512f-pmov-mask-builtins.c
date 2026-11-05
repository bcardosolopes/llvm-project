// RUN: %clang_cc1 -x c -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-apple-darwin -target-feature +avx512f -emit-llvm -o - -Wall -Werror -Wsign-conversion | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -x c -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-apple-darwin -target-feature +avx512f -fclangir -emit-llvm -o - -Wall -Werror -Wsign-conversion | FileCheck %s %}

#include <immintrin.h>

__m128i test_mm512_cvtepi32_epi8(__m512i a) {
  // CHECK-LABEL: test_mm512_cvtepi32_epi8
  // CHECK: trunc <16 x i32> %{{.*}} to <16 x i8>
  return _mm512_cvtepi32_epi8(a);
}

__m128i test_mm512_mask_cvtepi32_epi8(__m128i passthru, __mmask16 mask,
                                      __m512i a) {
  // CHECK-LABEL: test_mm512_mask_cvtepi32_epi8
  // CHECK: @llvm.x86.avx512.mask.pmov.db.512
  return _mm512_mask_cvtepi32_epi8(passthru, mask, a);
}

__m128i test_mm512_maskz_cvtepi32_epi8(__mmask16 mask, __m512i a) {
  // CHECK-LABEL: test_mm512_maskz_cvtepi32_epi8
  // CHECK: @llvm.x86.avx512.mask.pmov.db.512
  return _mm512_maskz_cvtepi32_epi8(mask, a);
}

__m256i test_mm512_cvtepi32_epi16(__m512i a) {
  // CHECK-LABEL: test_mm512_cvtepi32_epi16
  // CHECK: trunc <16 x i32> %{{.*}} to <16 x i16>
  return _mm512_cvtepi32_epi16(a);
}

__m256i test_mm512_mask_cvtepi32_epi16(__m256i passthru, __mmask16 mask,
                                       __m512i a) {
  // CHECK-LABEL: test_mm512_mask_cvtepi32_epi16
  // CHECK: @llvm.x86.avx512.mask.pmov.dw.512
  return _mm512_mask_cvtepi32_epi16(passthru, mask, a);
}

__m256i test_mm512_maskz_cvtepi32_epi16(__mmask16 mask, __m512i a) {
  // CHECK-LABEL: test_mm512_maskz_cvtepi32_epi16
  // CHECK: @llvm.x86.avx512.mask.pmov.dw.512
  return _mm512_maskz_cvtepi32_epi16(mask, a);
}

__m128i test_mm512_cvtepi64_epi16(__m512i a) {
  // CHECK-LABEL: test_mm512_cvtepi64_epi16
  // CHECK: trunc <8 x i64> %{{.*}} to <8 x i16>
  return _mm512_cvtepi64_epi16(a);
}

__m128i test_mm512_mask_cvtepi64_epi16(__m128i passthru, __mmask8 mask,
                                       __m512i a) {
  // CHECK-LABEL: test_mm512_mask_cvtepi64_epi16
  // CHECK: @llvm.x86.avx512.mask.pmov.qw.512
  return _mm512_mask_cvtepi64_epi16(passthru, mask, a);
}

__m128i test_mm512_maskz_cvtepi64_epi16(__mmask8 mask, __m512i a) {
  // CHECK-LABEL: test_mm512_maskz_cvtepi64_epi16
  // CHECK: @llvm.x86.avx512.mask.pmov.qw.512
  return _mm512_maskz_cvtepi64_epi16(mask, a);
}
