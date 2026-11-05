// RUN: %clang_cc1 -x c -ffreestanding -triple x86_64-unknown-linux -target-feature +avx2 -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -x c -ffreestanding -triple x86_64-unknown-linux -target-feature +avx2 -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test: AVX2 gather intrinsics require pointer type handling in
// decodeFixedType. Previously crashed with SIGSEGV because PointerType::get
// was called with null pointee type.

#include <immintrin.h>

__m128i test_i32gather_epi32(int const *base, __m128i idx) {
  // CIR-LABEL: @test_i32gather_epi32
  // CIR: cir.call_llvm_intrinsic "x86.avx2.gather.d.d"
  // LLVM-LABEL: @test_i32gather_epi32
  // LLVM: call <4 x i32> @llvm.x86.avx2.gather.d.d
  return _mm_i32gather_epi32(base, idx, 4);
}

__m256i test_i32gather256_epi32(int const *base, __m256i idx) {
  // CIR-LABEL: @test_i32gather256_epi32
  // CIR: cir.call_llvm_intrinsic "x86.avx2.gather.d.d.256"
  // LLVM-LABEL: @test_i32gather256_epi32
  // LLVM: call <8 x i32> @llvm.x86.avx2.gather.d.d.256
  return _mm256_i32gather_epi32(base, idx, 4);
}
