// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512vl -target-feature +avx512vbmi2 -fclangir -emit-cir -o %t.cir -Wall -Werror -Wsign-conversion 
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512vl -target-feature +avx512vbmi2 -fclangir -emit-llvm -o %t.ll -Wall -Werror -Wsign-conversion
// RUN: FileCheck --check-prefixes=LLVM --input-file=%t.ll %s

#include <immintrin.h>

__m128i test_mm_mask_expandloadu_epi16(__m128i __S, __mmask8 __U, void const* __P) {
  // CIR-LABEL: _mm_mask_expandloadu_epi16
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<8 x !s16i>>, !cir.vector<8 x !cir.int<s, 1>>, !cir.vector<8 x !s16i>) -> !cir.vector<8 x !s16i>

  // LLVM-LABEL: @test_mm_mask_expandloadu_epi16
  // LLVM: @llvm.masked.expandload.v8i16(ptr %{{.*}}, <8 x i1> %{{.*}}, <8 x i16> %{{.*}})
  return _mm_mask_expandloadu_epi16(__S, __U, __P);
}

__m128i test_mm_maskz_expandloadu_epi16(__mmask8 __U, void const* __P) {
  // CIR-LABEL: _mm_maskz_expandloadu_epi16
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<8 x !s16i>>, !cir.vector<8 x !cir.int<s, 1>>, !cir.vector<8 x !s16i>) -> !cir.vector<8 x !s16i>

  // LLVM-LABEL: @test_mm_maskz_expandloadu_epi16
  // LLVM: @llvm.masked.expandload.v8i16(ptr %{{.*}}, <8 x i1> %{{.*}}, <8 x i16> %{{.*}})
  return _mm_maskz_expandloadu_epi16(__U, __P);
}

__m256i test_mm256_mask_expandloadu_epi16(__m256i __S, __mmask16 __U, void const* __P) {
  // CIR-LABEL: _mm256_mask_expandloadu_epi16
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<16 x !s16i>>, !cir.vector<16 x !cir.int<s, 1>>, !cir.vector<16 x !s16i>) -> !cir.vector<16 x !s16i>

  // LLVM-LABEL: @test_mm256_mask_expandloadu_epi16
  // LLVM: @llvm.masked.expandload.v16i16(ptr %{{.*}}, <16 x i1> %{{.*}}, <16 x i16> %{{.*}})
  return _mm256_mask_expandloadu_epi16(__S, __U, __P);
}

__m256i test_mm256_maskz_expandloadu_epi16(__mmask16 __U, void const* __P) {
  // CIR-LABEL: _mm256_maskz_expandloadu_epi16
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<16 x !s16i>>, !cir.vector<16 x !cir.int<s, 1>>, !cir.vector<16 x !s16i>) -> !cir.vector<16 x !s16i>

  // LLVM-LABEL: @test_mm256_maskz_expandloadu_epi16
  // LLVM: @llvm.masked.expandload.v16i16(ptr %{{.*}}, <16 x i1> %{{.*}}, <16 x i16> %{{.*}})
return _mm256_maskz_expandloadu_epi16(__U, __P);
}

__m128i test_mm_mask_expandloadu_epi8(__m128i __S, __mmask16 __U, void const* __P) {
   // CIR-LABEL: _mm_mask_expandloadu_epi8
   // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<16 x !s8i>>, !cir.vector<16 x !cir.int<s, 1>>, !cir.vector<16 x !s8i>) -> !cir.vector<16 x !s8i>

   // LLVM-LABEL: @test_mm_mask_expandloadu_epi8
   // LLVM: @llvm.masked.expandload.v16i8(ptr %{{.*}}, <16 x i1> %{{.*}}, <16 x i8> %{{.*}})
   return _mm_mask_expandloadu_epi8(__S, __U, __P);
}

__m128i test_mm_maskz_expandloadu_epi8(__mmask16 __U, void const* __P) {
   // CIR-LABEL: _mm_maskz_expandloadu_epi8
   // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<16 x !s8i>>, !cir.vector<16 x !cir.int<s, 1>>, !cir.vector<16 x !s8i>) -> !cir.vector<16 x !s8i>

   // LLVM-LABEL: @test_mm_maskz_expandloadu_epi8
   // LLVM: @llvm.masked.expandload.v16i8(ptr %{{.*}}, <16 x i1> %{{.*}}, <16 x i8> %{{.*}})
return _mm_maskz_expandloadu_epi8(__U, __P);
}

__m256i test_mm256_mask_expandloadu_epi8(__m256i __S, __mmask32 __U, void const* __P) {
  // CIR-LABEL: _mm256_mask_expandloadu_epi8
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<32 x !s8i>>, !cir.vector<32 x !cir.int<s, 1>>, !cir.vector<32 x !s8i>) -> !cir.vector<32 x !s8i>

  // LLVM-LABEL: @test_mm256_mask_expandloadu_epi8
  // LLVM: @llvm.masked.expandload.v32i8(ptr %{{.*}}, <32 x i1> %{{.*}}, <32 x i8> %{{.*}})
  return _mm256_mask_expandloadu_epi8(__S, __U, __P);
}

__m256i test_mm256_maskz_expandloadu_epi8(__mmask32 __U, void const* __P) {
   // CIR-LABEL: _mm256_maskz_expandloadu_epi8
   // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<32 x !s8i>>, !cir.vector<32 x !cir.int<s, 1>>, !cir.vector<32 x !s8i>) -> !cir.vector<32 x !s8i>

   // LLVM-LABEL: @test_mm256_maskz_expandloadu_epi8
   // LLVM: @llvm.masked.expandload.v32i8(ptr %{{.*}}, <32 x i1> %{{.*}}, <32 x i8> %{{.*}})
   return _mm256_maskz_expandloadu_epi8(__U, __P);
}

void test_mm256_mask_compressstoreu_epi16(void *__P, __mmask16 __U, __m256i __D) {
  // CIR-LABEL: _mm256_mask_compressstoreu_epi16
  // CIR: cir.call_llvm_intrinsic "masked.compressstore" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.vector<16 x !s16i>, !cir.ptr<!cir.vector<16 x !s16i>>, !cir.vector<16 x !cir.int<s, 1>>) -> !void

  // LLVM-LABEL: @test_mm256_mask_compressstoreu_epi16
  // LLVM: @llvm.masked.compressstore.v16i16(<16 x i16> %{{.*}}, ptr %{{.*}}, <16 x i1> %{{.*}})
  _mm256_mask_compressstoreu_epi16(__P, __U, __D);
}

void test_mm_mask_compressstoreu_epi8(void *__P, __mmask16 __U, __m128i __D) {
  // CIR-LABEL: _mm_mask_compressstoreu_epi8
  // CIR: cir.call_llvm_intrinsic "masked.compressstore" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.vector<16 x !s8i>, !cir.ptr<!cir.vector<16 x !s8i>>, !cir.vector<16 x !cir.int<s, 1>>) -> !void

  // LLVM-LABEL: @test_mm_mask_compressstoreu_epi8
  // LLVM: @llvm.masked.compressstore.v16i8(<16 x i8> %{{.*}}, ptr %{{.*}}, <16 x i1> %{{.*}})
  _mm_mask_compressstoreu_epi8(__P, __U, __D);
}

void test_mm256_mask_compressstoreu_epi8(void *__P, __mmask32 __U, __m256i __D) {
  // CIR-LABEL: _mm256_mask_compressstoreu_epi8
  // CIR: cir.call_llvm_intrinsic "masked.compressstore" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.vector<32 x !s8i>, !cir.ptr<!cir.vector<32 x !s8i>>, !cir.vector<32 x !cir.int<s, 1>>) -> !void

  // LLVM-LABEL: @test_mm256_mask_compressstoreu_epi8
  // LLVM: @llvm.masked.compressstore.v32i8(<32 x i8> %{{.*}}, ptr %{{.*}}, <32 x i1> %{{.*}})
  _mm256_mask_compressstoreu_epi8(__P, __U, __D);
}