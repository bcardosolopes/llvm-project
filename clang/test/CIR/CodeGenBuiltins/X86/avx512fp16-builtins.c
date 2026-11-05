// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512fp16 -fclangir -emit-mlir=cir-abi -o %t.cir -Wall -Werror
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512fp16 -fclangir -emit-llvm -o %t.ll  -Wall -Werror
// RUN: FileCheck --check-prefixes=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -x c -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512fp16 -emit-llvm -o - -Wall -Werror | FileCheck %s -check-prefix=OGCG
// RUN: %clang_cc1 -x c++ -flax-vector-conversions=none -ffreestanding %s -triple=x86_64-unknown-linux -target-feature +avx512fp16 -emit-llvm -o - -Wall -Werror | FileCheck %s -check-prefix=OGCG

#include <immintrin.h>

__m128h test_mm_undefined_ph(void) {
  // CIR-LABEL: _mm_undefined_ph
  // CIR: %[[A:.*]] = cir.const #cir.zero : !cir.vector<2 x !cir.double>
  // CIR: %{{.*}} = cir.cast bitcast %[[A]] : !cir.vector<2 x !cir.double> -> !cir.vector<8 x !cir.f16>
  // CIR: cir.return %{{.*}} : !cir.vector<8 x !cir.f16>

  // CIR-LABEL: cir.func {{.*}}test_mm_undefined_ph
  // CIR: call @_mm_undefined_ph

  // LLVM-LABEL: @test_mm_undefined_ph
  // LLVM: ret <8 x half> zeroinitializer

  // OGCG-LABEL: test_mm_undefined_ph
  // OGCG: ret <8 x half> zeroinitializer
  return _mm_undefined_ph();
}

__m256h test_mm256_undefined_ph(void) {
  // CIR-LABEL: _mm256_undefined_ph
  // CIR: %[[A:.*]] = cir.const #cir.zero : !cir.vector<4 x !cir.double>
  // CIR: %{{.*}} = cir.cast bitcast %[[A]] : !cir.vector<4 x !cir.double> -> !cir.vector<16 x !cir.f16>
  // CIR: cir.return %{{.*}} : !cir.vector<16 x !cir.f16>

  // CIR-LABEL: cir.func {{.*}}test_mm256_undefined_ph
  // CIR: call @_mm256_undefined_ph

  // LLVM-LABEL: @test_mm256_undefined_ph
  // LLVM: ret <16 x half> zeroinitializer

  // OGCG-LABEL: test_mm256_undefined_ph
  // OGCG: ret <16 x half> zeroinitializer
  return _mm256_undefined_ph();
}

__m512h test_mm512_undefined_ph(void) {
  // CIR-LABEL: _mm512_undefined_ph
  // CIR: %[[A:.*]] = cir.const #cir.zero : !cir.vector<8 x !cir.double>
  // CIR: %{{.*}} = cir.cast bitcast %[[A]] : !cir.vector<8 x !cir.double> -> !cir.vector<32 x !cir.f16>
  // CIR: cir.return %{{.*}} : !cir.vector<32 x !cir.f16>

  // CIR-LABEL: cir.func {{.*}}test_mm512_undefined_ph
  // CIR: call @_mm512_undefined_ph

  // LLVM-LABEL: @test_mm512_undefined_ph
  // LLVM: ret <32 x half> zeroinitializer

  // OGCG-LABEL: test_mm512_undefined_ph
  // OGCG: ret <32 x half> zeroinitializer
  return _mm512_undefined_ph();
}

_Float16 test_mm512_reduce_add_ph(__m512h __W) {
  // CIR-LABEL: _mm512_reduce_add_ph
  // CIR: cir.call_llvm_intrinsic "vector.reduce.fadd" %[[R:.*]], %[[V:.*]] : (!cir.f16, !cir.vector<32 x !cir.f16>) -> !cir.f16

  // CIR-LABEL: test_mm512_reduce_add_ph
  // CIR: cir.call @_mm512_reduce_add_ph(%[[VEC:.*]]) : (!cir.vector<32 x !cir.f16>) -> !cir.f16

  // LLVM-LABEL: test_mm512_reduce_add_ph
  // LLVM: call {{.*}}half @llvm.vector.reduce.fadd.v32f16(half 0xH8000, <32 x half> %{{.*}})

  // OGCG-LABEL: test_mm512_reduce_add_ph
  // OGCG: call reassoc {{.*}}half @llvm.vector.reduce.fadd.v32f16(half 0xH8000, <32 x half> %{{.*}})
  return _mm512_reduce_add_ph(__W);
}

_Float16 test_mm512_reduce_mul_ph(__m512h __W) {
  // CIR-LABEL: _mm512_reduce_mul_ph
  // CIR: cir.call_llvm_intrinsic "vector.reduce.fmul" %[[R:.*]], %[[V:.*]] : (!cir.f16, !cir.vector<32 x !cir.f16>) -> !cir.f16

  // CIR-LABEL: test_mm512_reduce_mul_ph
  // CIR: cir.call @_mm512_reduce_mul_ph(%[[VEC:.*]]) : (!cir.vector<32 x !cir.f16>) -> !cir.f16

  // LLVM-LABEL: test_mm512_reduce_mul_ph
  // LLVM: call {{.*}}half @llvm.vector.reduce.fmul.v32f16(half 0xH3C00, <32 x half> %{{.*}})

  // OGCG-LABEL: test_mm512_reduce_mul_ph
  // OGCG: call reassoc {{.*}}half @llvm.vector.reduce.fmul.v32f16(half 0xH3C00, <32 x half> %{{.*}})
  return _mm512_reduce_mul_ph(__W);
}

_Float16 test_mm512_reduce_max_ph(__m512h __W) {
  // CIR-LABEL: _mm512_reduce_max_ph
  // CIR: cir.call_llvm_intrinsic "vector.reduce.fmax" %[[V:.*]] (!cir.vector<32 x !cir.f16>) -> !cir.f16 

  // CIR-LABEL: test_mm512_reduce_max_ph
  // CIR: cir.call @_mm512_reduce_max_ph(%[[VEC:.*]]) : (!cir.vector<32 x !cir.f16>) -> !cir.f16

  // LLVM-LABEL: test_mm512_reduce_max_ph
  // LLVM: call {{.*}}half @llvm.vector.reduce.fmax.v32f16(<32 x half> %{{.*}})

  // OGCG-LABEL: test_mm512_reduce_max_ph
  // OGCG: call nnan {{.*}}half @llvm.vector.reduce.fmax.v32f16(<32 x half> %{{.*}})
  return _mm512_reduce_max_ph(__W);
}

_Float16 test_mm512_reduce_min_ph(__m512h __W) {
  // CIR-LABEL: _mm512_reduce_min_ph
  // CIR: cir.call_llvm_intrinsic "vector.reduce.fmin" %[[V:.*]] (!cir.vector<32 x !cir.f16>) -> !cir.f16 

  // CIR-LABEL: test_mm512_reduce_min_ph
  // CIR: cir.call @_mm512_reduce_min_ph(%[[VEC:.*]]) : (!cir.vector<32 x !cir.f16>) -> !cir.f16

  // LLVM-LABEL: test_mm512_reduce_min_ph
  // LLVM: call {{.*}}half @llvm.vector.reduce.fmin.v32f16(<32 x half> %{{.*}})

  // OGCG-LABEL: test_mm512_reduce_min_ph
  // OGCG: call nnan {{.*}}half @llvm.vector.reduce.fmin.v32f16(<32 x half> %{{.*}})
  return _mm512_reduce_min_ph(__W);
}


__mmask32 test_mm512_mask_fpclass_ph_mask(__mmask32 __U, __m512h __A) {
  // CIR-LABEL: _mm512_mask_fpclass_ph_mask
  // CIR: %[[A:.*]] = cir.call_llvm_intrinsic "x86.avx512fp16.fpclass.ph.512"
  // CIR: %[[B:.*]] = cir.cast bitcast {{.*}} : !u32i -> !cir.vector<32 x !cir.int<s, 1>>
  // CIR: %[[C:.*]] = cir.binop(and, %[[A]], %[[B]]) : !cir.vector<32 x !cir.int<s, 1>>
  // CIR: cir.cast bitcast %[[C]] : !cir.vector<32 x !cir.int<s, 1>> -> !u32i

  // LLVM-LABEL: test_mm512_mask_fpclass_ph_mask
  // LLVM: %[[A:.*]] = call <32 x i1> @llvm.x86.avx512fp16.fpclass.ph.512
  // LLVM: %[[B:.*]] = bitcast i32 {{.*}} to <32 x i1>
  // LLVM: %[[C:.*]] = and <32 x i1> %[[A]], %[[B]]
  // LLVM: bitcast <32 x i1> %[[C]] to i32

  // OGCG-LABEL: test_mm512_mask_fpclass_ph_mask
  // OGCG: %[[A:.*]] = call <32 x i1> @llvm.x86.avx512fp16.fpclass.ph.512
  // OGCG: %[[B:.*]] = bitcast i32 {{.*}} to <32 x i1>
  // OGCG: %[[C:.*]] = and <32 x i1> %[[A]], %[[B]]
  // OGCG: bitcast <32 x i1> %[[C]] to i32
  return _mm512_mask_fpclass_ph_mask(__U, __A, 4);
}

__mmask32 test_mm512_fpclass_ph_mask(__m512h __A) {
  // CIR-LABEL: _mm512_fpclass_ph_mask
  // CIR: %[[A:.*]] = cir.call_llvm_intrinsic "x86.avx512fp16.fpclass.ph.512"
  // CIR: cir.cast bitcast %[[A]] : !cir.vector<32 x !cir.int<s, 1>> -> !u32i

  // LLVM-LABEL: test_mm512_fpclass_ph_mask
  // LLVM: %[[A:.*]] = call <32 x i1> @llvm.x86.avx512fp16.fpclass.ph.512
  // LLVM: bitcast <32 x i1> %[[A]] to i32

  // OGCG-LABEL: test_mm512_fpclass_ph_mask
  // OGCG: %[[A:.*]] = call <32 x i1> @llvm.x86.avx512fp16.fpclass.ph.512
  // OGCG: bitcast <32 x i1> %[[A]] to i32
  return _mm512_fpclass_ph_mask(__A, 4);
}

__m512h test_mm512_mask_cvt_roundepi16_ph(__m512h __W, __mmask32 __U,
                                          __m512i __A) {
  // CIR-LABEL: test_mm512_mask_cvt_roundepi16_ph
  // CIR: %[[CVT:.*]] = cir.call_llvm_intrinsic "x86.avx512.sitofp.round" %{{.*}}, %{{.*}} : (!cir.vector<32 x !s16i>, !s32i) -> !cir.vector<32 x !cir.f16>
  // CIR: %[[MASK:.*]] = cir.cast bitcast %{{.*}} : !u32i -> !cir.vector<32 x !cir.int<s, 1>>
  // CIR: %[[SEL:.*]] = cir.vec.ternary(%[[MASK]], %[[CVT]], %{{.*}}) : !cir.vector<32 x !cir.int<s, 1>>, !cir.vector<32 x !cir.f16>
  // CIR: cir.return %[[SEL]] : !cir.vector<32 x !cir.f16>

  // LLVM-LABEL: @test_mm512_mask_cvt_roundepi16_ph
  // LLVM: %[[CVT:.*]] = call <32 x half> @llvm.x86.avx512.sitofp.round.v32f16.v32i16(<32 x i16> %{{.*}}, i32 11)
  // LLVM: %[[MASKBITS:.*]] = bitcast i32 %{{.*}} to <32 x i1>
  // LLVM: %[[MASK:.*]] = icmp ne <32 x i1> %[[MASKBITS]], zeroinitializer
  // LLVM: %[[SEL:.*]] = select <32 x i1> %[[MASK]], <32 x half> %[[CVT]], <32 x half> %{{.*}}
  // LLVM: ret <32 x half> %[[SEL]]

  // OGCG-LABEL: test_mm512_mask_cvt_roundepi16_ph
  // OGCG: %[[CVT:.*]] = call <32 x half> @llvm.x86.avx512.sitofp.round.v32f16.v32i16(<32 x i16> %{{.*}}, i32 11)
  // OGCG: %[[MASK:.*]] = bitcast i32 %{{.*}} to <32 x i1>
  // OGCG: %[[SEL:.*]] = select <32 x i1> %[[MASK]], <32 x half> %[[CVT]], <32 x half> %{{.*}}
  // OGCG: ret <32 x half> %[[SEL]]
  return _mm512_mask_cvt_roundepi16_ph(
      __W, __U, __A, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
}

__m512h test_mm512_maskz_cvt_roundepi16_ph(__mmask32 __U, __m512i __A) {
  // CIR-LABEL: test_mm512_maskz_cvt_roundepi16_ph
  // CIR: %[[ZERO:.*]] = cir.call @_mm512_setzero_ph() : () -> !cir.vector<32 x !cir.f16>
  // CIR: %[[CVT:.*]] = cir.call_llvm_intrinsic "x86.avx512.sitofp.round" %{{.*}}, %{{.*}} : (!cir.vector<32 x !s16i>, !s32i) -> !cir.vector<32 x !cir.f16>
  // CIR: %[[MASK:.*]] = cir.cast bitcast %{{.*}} : !u32i -> !cir.vector<32 x !cir.int<s, 1>>
  // CIR: %[[SEL:.*]] = cir.vec.ternary(%[[MASK]], %[[CVT]], %[[ZERO]]) : !cir.vector<32 x !cir.int<s, 1>>, !cir.vector<32 x !cir.f16>
  // CIR: cir.return %[[SEL]] : !cir.vector<32 x !cir.f16>

  // LLVM-LABEL: @test_mm512_maskz_cvt_roundepi16_ph
  // LLVM: %[[CVT:.*]] = call <32 x half> @llvm.x86.avx512.sitofp.round.v32f16.v32i16(<32 x i16> %{{.*}}, i32 11)
  // LLVM: %[[MASKBITS:.*]] = bitcast i32 %{{.*}} to <32 x i1>
  // LLVM: %[[MASK:.*]] = icmp ne <32 x i1> %[[MASKBITS]], zeroinitializer
  // LLVM: %[[SEL:.*]] = select <32 x i1> %[[MASK]], <32 x half> %[[CVT]], <32 x half> zeroinitializer
  // LLVM: ret <32 x half> %[[SEL]]

  // OGCG-LABEL: test_mm512_maskz_cvt_roundepi16_ph
  // OGCG: %[[CVT:.*]] = call <32 x half> @llvm.x86.avx512.sitofp.round.v32f16.v32i16(<32 x i16> %{{.*}}, i32 11)
  // OGCG: %[[MASK:.*]] = bitcast i32 %{{.*}} to <32 x i1>
  // OGCG: %[[SEL:.*]] = select <32 x i1> %[[MASK]], <32 x half> %[[CVT]], <32 x half> %{{.*}}
  // OGCG: ret <32 x half> %[[SEL]]
  return _mm512_maskz_cvt_roundepi16_ph(
      __U, __A, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
}
