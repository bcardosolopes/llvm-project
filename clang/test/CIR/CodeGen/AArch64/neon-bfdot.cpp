// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +v8a \
// RUN:   -target-feature +fp-armv8 -target-feature +neon -fclangir \
// RUN:   -emit-cir -std=gnu++20 -w %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +v8a \
// RUN:   -target-feature +fp-armv8 -target-feature +neon -fclangir \
// RUN:   -emit-llvm -std=gnu++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

typedef __attribute__((neon_vector_type(4))) float float32x4_t;
typedef __attribute__((neon_vector_type(2))) float float32x2_t;
typedef __bf16 bfloat16_t;
typedef __attribute__((neon_vector_type(8))) bfloat16_t bfloat16x8_t;
typedef __attribute__((neon_vector_type(4))) bfloat16_t bfloat16x4_t;
typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;
typedef __attribute__((neon_vector_type(8))) signed char int8x8_t;

// CIR-LABEL: @_Z15test_bfdotq_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.bfdot"
// CIR-SAME: (!cir.vector<4 x !cir.float>, !cir.vector<8 x !cir.bf16>, !cir.vector<8 x !cir.bf16>) -> !cir.vector<4 x !cir.float>

// LLVM-LABEL: @_Z15test_bfdotq_f32
// LLVM: call <4 x float> @llvm.aarch64.neon.bfdot.v4f32.v8bf16(<4 x float> {{.*}}, <8 x bfloat> {{.*}}, <8 x bfloat> {{.*}})
__attribute__((target("bf16,neon")))
float32x4_t test_bfdotq_f32(float32x4_t a, bfloat16x8_t b, bfloat16x8_t c) {
    return __builtin_bit_cast(float32x4_t, __builtin_neon_vbfdotq_f32(
        __builtin_bit_cast(int8x16_t, a),
        __builtin_bit_cast(int8x16_t, b),
        __builtin_bit_cast(int8x16_t, c), 41));
}

// CIR-LABEL: @_Z14test_bfdot_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.bfdot"
// CIR-SAME: (!cir.vector<2 x !cir.float>, !cir.vector<4 x !cir.bf16>, !cir.vector<4 x !cir.bf16>) -> !cir.vector<2 x !cir.float>

// LLVM-LABEL: @_Z14test_bfdot_f32
// LLVM: call <2 x float> @llvm.aarch64.neon.bfdot.v2f32.v4bf16(<2 x float> {{.*}}, <4 x bfloat> {{.*}}, <4 x bfloat> {{.*}})
__attribute__((target("bf16,neon")))
float32x2_t test_bfdot_f32(float32x2_t a, bfloat16x4_t b, bfloat16x4_t c) {
    return __builtin_bit_cast(float32x2_t, __builtin_neon_vbfdot_f32(
        __builtin_bit_cast(int8x8_t, a),
        __builtin_bit_cast(int8x8_t, b),
        __builtin_bit_cast(int8x8_t, c), 9));
}
