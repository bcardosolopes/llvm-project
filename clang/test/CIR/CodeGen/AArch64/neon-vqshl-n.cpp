// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +v8a \
// RUN:   -target-feature +fp-armv8 -target-feature +neon -fclangir \
// RUN:   -emit-cir -w %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -target-feature +v8a \
// RUN:   -target-feature +fp-armv8 -target-feature +neon -fclangir \
// RUN:   -emit-llvm -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

typedef __attribute__((neon_vector_type(4))) unsigned int uint32x4_t;
typedef __attribute__((neon_vector_type(4))) int int32x4_t;
typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;

// Test unsigned saturating shift left by immediate (vqshlq_n_u32).
// The shift amount is a scalar that must be splatted into a vector, not bitcast.
// Type flag 50 = unsigned(bit4) + quad(bit5) + Int32(bits0-3).

// CIR-LABEL: @_Z16test_vqshl_n_u32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqshl"
// CIR-SAME: (!cir.vector<4 x !u32i>, !cir.vector<4 x !u32i>) -> !cir.vector<4 x !u32i>

// LLVM-LABEL: @_Z16test_vqshl_n_u32
// LLVM: call <4 x i32> @llvm.aarch64.neon.uqshl.v4i32(<4 x i32> {{.*}}, <4 x i32> splat (i32 6))
uint32x4_t test_vqshl_n_u32(uint32x4_t a) {
    return __builtin_bit_cast(uint32x4_t, __builtin_neon_vqshlq_n_v(
        __builtin_bit_cast(int8x16_t, a), 6, 50));
}

// Test signed saturating shift left by immediate (vqshlq_n_s32).
// Type flag 34 = quad(bit5) + Int32(bits0-3), unsigned=0.

// CIR-LABEL: @_Z16test_vqshl_n_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshl"

// LLVM-LABEL: @_Z16test_vqshl_n_s32
// LLVM: call <4 x i32> @llvm.aarch64.neon.sqshl.v4i32(<4 x i32> {{.*}}, <4 x i32> splat (i32 3))
int32x4_t test_vqshl_n_s32(int32x4_t a) {
    return __builtin_bit_cast(int32x4_t, __builtin_neon_vqshlq_n_v(
        __builtin_bit_cast(int8x16_t, a), 3, 34));
}
