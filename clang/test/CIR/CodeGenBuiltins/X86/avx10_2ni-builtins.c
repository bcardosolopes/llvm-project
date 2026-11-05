// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx10.2 -fclangir -emit-cir -o %t.cir -Wall -Werror
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx10.2 -fclangir -emit-llvm -o %t.ll -Wall -Werror
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -x c -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx10.2 -emit-llvm -o - -Wall -Werror | FileCheck %s -check-prefix=OGCG

#include <immintrin.h>

__m128i test_mm_mpsadbw_epu8(__m128i __A, __m128i __B) {
  // CIR-LABEL: test_mm_mpsadbw_epu8
  // CIR: %[[A:.*]] = cir.cast bitcast %{{.*}} : !cir.vector<2 x !s64i> -> !cir.vector<16 x !s8i>
  // CIR: %[[B:.*]] = cir.cast bitcast %{{.*}} : !cir.vector<2 x !s64i> -> !cir.vector<16 x !s8i>
  // CIR: %[[RES:.*]] = cir.call_llvm_intrinsic "x86.sse41.mpsadbw" %[[A]], %[[B]], %{{.*}} : (!cir.vector<16 x !s8i>, !cir.vector<16 x !s8i>, !s8i) -> !cir.vector<8 x !s16i>
  // CIR: %[[BC:.*]] = cir.cast bitcast %[[RES]] : !cir.vector<8 x !s16i> -> !cir.vector<16 x !s8i>
  // CIR: %{{.*}} = cir.cast bitcast %[[BC]] : !cir.vector<16 x !s8i> -> !cir.vector<2 x !s64i>

  // LLVM-LABEL: @test_mm_mpsadbw_epu8
  // LLVM: %[[RES:.*]] = call <8 x i16> @llvm.x86.sse41.mpsadbw(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, i8 -86)
  // LLVM: %[[BC:.*]] = bitcast <8 x i16> %[[RES]] to <16 x i8>
  // LLVM: %{{.*}} = bitcast <16 x i8> %[[BC]] to <2 x i64>

  // OGCG-LABEL: @test_mm_mpsadbw_epu8
  // OGCG: %[[RES:.*]] = call <8 x i16> @llvm.x86.sse41.mpsadbw(<16 x i8> %{{.*}}, <16 x i8> %{{.*}}, i8 -86)
  // OGCG: %[[BC:.*]] = bitcast <8 x i16> %[[RES]] to <16 x i8>
  // OGCG: %{{.*}} = bitcast <16 x i8> %[[BC]] to <2 x i64>
  return _mm_mpsadbw_epu8(__A, __B, 170);
}

__m256i test_mm256_mpsadbw_epu8(__m256i __A, __m256i __B) {
  // CIR-LABEL: test_mm256_mpsadbw_epu8
  // CIR: %[[A:.*]] = cir.cast bitcast %{{.*}} : !cir.vector<4 x !s64i> -> !cir.vector<32 x !s8i>
  // CIR: %[[B:.*]] = cir.cast bitcast %{{.*}} : !cir.vector<4 x !s64i> -> !cir.vector<32 x !s8i>
  // CIR: %[[RES:.*]] = cir.call_llvm_intrinsic "x86.avx2.mpsadbw" %[[A]], %[[B]], %{{.*}} : (!cir.vector<32 x !s8i>, !cir.vector<32 x !s8i>, !s8i) -> !cir.vector<16 x !s16i>
  // CIR: %[[BC:.*]] = cir.cast bitcast %[[RES]] : !cir.vector<16 x !s16i> -> !cir.vector<32 x !s8i>
  // CIR: %{{.*}} = cir.cast bitcast %[[BC]] : !cir.vector<32 x !s8i> -> !cir.vector<4 x !s64i>

  // LLVM-LABEL: @test_mm256_mpsadbw_epu8
  // LLVM: %[[RES:.*]] = call <16 x i16> @llvm.x86.avx2.mpsadbw(<32 x i8> %{{.*}}, <32 x i8> %{{.*}}, i8 -86)
  // LLVM: %[[BC:.*]] = bitcast <16 x i16> %[[RES]] to <32 x i8>
  // LLVM: %{{.*}} = bitcast <32 x i8> %[[BC]] to <4 x i64>

  // OGCG-LABEL: @test_mm256_mpsadbw_epu8
  // OGCG: %[[RES:.*]] = call <16 x i16> @llvm.x86.avx2.mpsadbw(<32 x i8> %{{.*}}, <32 x i8> %{{.*}}, i8 -86)
  // OGCG: %[[BC:.*]] = bitcast <16 x i16> %[[RES]] to <32 x i8>
  // OGCG: %{{.*}} = bitcast <32 x i8> %[[BC]] to <4 x i64>
  return _mm256_mpsadbw_epu8(__A, __B, 170);
}
