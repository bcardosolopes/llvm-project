// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512vbmi2 -fclangir -emit-cir -o %t.cir -Wall -Werror -Wsign-conversion 
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512vbmi2 -fclangir -emit-llvm -o %t.ll -Wall -Werror -Wsign-conversion
// RUN: FileCheck --check-prefixes=LLVM --input-file=%t.ll %s

#include <immintrin.h>

__m512i test_mm512_mask_expandloadu_epi16(__m512i __S, __mmask32 __U, void const* __P) {
  // CIR-LABEL: _mm512_mask_expandloadu_epi16
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<32 x !s16i>>, !cir.vector<32 x !cir.int<s, 1>>, !cir.vector<32 x !s16i>) -> !cir.vector<32 x !s16i>

  // LLVM-LABEL: @test_mm512_mask_expandloadu_epi16
  // LLVM: @llvm.masked.expandload.v32i16(ptr %{{.*}}, <32 x i1> %{{.*}}, <32 x i16> %{{.*}})
  return _mm512_mask_expandloadu_epi16(__S, __U, __P);
}

__m512i test_mm512_maskz_expandloadu_epi16(__mmask32 __U, void const* __P) {
  // CIR-LABEL: _mm512_maskz_expandloadu_epi16
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<32 x !s16i>>, !cir.vector<32 x !cir.int<s, 1>>, !cir.vector<32 x !s16i>) -> !cir.vector<32 x !s16i>

  // LLVM-LABEL: @test_mm512_maskz_expandloadu_epi16
  // LLVM: @llvm.masked.expandload.v32i16(ptr %{{.*}}, <32 x i1> %{{.*}}, <32 x i16> %{{.*}})
  return _mm512_maskz_expandloadu_epi16(__U, __P);
}

__m512i test_mm512_mask_expandloadu_epi8(__m512i __S, __mmask64 __U, void const* __P) {
  // CIR-LABEL: _mm512_mask_expandloadu_epi8
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<64 x !s8i>>, !cir.vector<64 x !cir.int<s, 1>>, !cir.vector<64 x !s8i>) -> !cir.vector<64 x !s8i>

  // LLVM-LABEL: @test_mm512_mask_expandloadu_epi8
  // LLVM: @llvm.masked.expandload.v64i8(ptr %{{.*}}, <64 x i1> %{{.*}}, <64 x i8> %{{.*}})
  return _mm512_mask_expandloadu_epi8(__S, __U, __P);
}

__m512i test_mm512_maskz_expandloadu_epi8(__mmask64 __U, void const* __P) {
  // CIR-LABEL: _mm512_maskz_expandloadu_epi8
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "masked.expandload" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.ptr<!cir.vector<64 x !s8i>>, !cir.vector<64 x !cir.int<s, 1>>, !cir.vector<64 x !s8i>) -> !cir.vector<64 x !s8i>

  // LLVM-LABEL: @test_mm512_maskz_expandloadu_epi8
  // LLVM: @llvm.masked.expandload.v64i8(ptr %{{.*}}, <64 x i1> %{{.*}}, <64 x i8> %{{.*}})
  return _mm512_maskz_expandloadu_epi8(__U, __P);
}

void test_mm512_mask_compressstoreu_epi16(void *__P, __mmask32 __U, __m512i __D) {
  // CIR-LABEL: _mm512_mask_compressstoreu_epi16
  // CIR: cir.call_llvm_intrinsic "masked.compressstore" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.vector<32 x !s16i>, !cir.ptr<!cir.vector<32 x !s16i>>, !cir.vector<32 x !cir.int<s, 1>>) -> !void

  // LLVM-LABEL: @test_mm512_mask_compressstoreu_epi16
  // LLVM: @llvm.masked.compressstore.v32i16(<32 x i16> %{{.*}}, ptr %{{.*}}, <32 x i1> %{{.*}})
  _mm512_mask_compressstoreu_epi16(__P, __U, __D);
}

void test_mm512_mask_compressstoreu_epi8(void *__P, __mmask64 __U, __m512i __D) {
  // CIR-LABEL: _mm512_mask_compressstoreu_epi8
  // CIR: cir.call_llvm_intrinsic "masked.compressstore" %{{.*}}, %{{.*}}, %{{.*}} : (!cir.vector<64 x !s8i>, !cir.ptr<!cir.vector<64 x !s8i>>, !cir.vector<64 x !cir.int<s, 1>>) -> !void

  // LLVM-LABEL: @test_mm512_mask_compressstoreu_epi8
  // LLVM: @llvm.masked.compressstore.v64i8(<64 x i8> %{{.*}}, ptr %{{.*}}, <64 x i1> %{{.*}})
  _mm512_mask_compressstoreu_epi8(__P, __U, __D);
}
