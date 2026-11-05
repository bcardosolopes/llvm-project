// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm -target-feature +neon %s -o /dev/null

// NEON vcaltq_f32 (__builtin_neon_vcaltq_v) — fixed

typedef __attribute__((neon_vector_type(4))) float float32x4_t;
typedef __attribute__((neon_vector_type(4))) unsigned int uint32x4_t;
typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;

void test(float32x4_t a, float32x4_t b, uint32x4_t *out) {
  *out = __builtin_bit_cast(uint32x4_t,
    __builtin_neon_vcaltq_v(__builtin_bit_cast(int8x16_t, a),
                            __builtin_bit_cast(int8x16_t, b), 50));
}
