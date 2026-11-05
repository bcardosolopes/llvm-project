// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir \
// RUN:   -fno-clangir-call-conv-lowering -emit-llvm -std=c++20 -w \
// RUN:   -target-feature +neon %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll -check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -emit-llvm \
// RUN:   -std=c++20 -w -target-feature +neon %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll -check-prefix=OGCG

// Test that NEON compare-with-zero intrinsics produce sext (giving -1/0)
// without an extra negate.: the negate was inverting the sign,
// producing 0/1 instead of the correct 0/-1 (0xFFFFFFFF) pattern.

typedef float float32_t;
typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;
typedef __attribute__((neon_vector_type(4))) float32_t float32x4_t;
typedef __attribute__((neon_vector_type(4))) unsigned int uint32x4_t;
typedef __attribute__((neon_vector_type(4))) int int32x4_t;

// Float compare-with-zero: sext without negate
// LLVM-LABEL: define {{.*}} @_Z10test_float
// LLVM: fcmp oeq <4 x float> {{%.*}}, zeroinitializer
// LLVM: sext <4 x i1> {{%.*}} to <4 x i32>
// LLVM-NOT: sub <4 x i32> zeroinitializer
// OGCG-LABEL: define {{.*}} @_Z10test_float
// OGCG: fcmp oeq <4 x float> {{%.*}}, zeroinitializer
// OGCG: sext <4 x i1> {{%.*}} to <4 x i32>
uint32x4_t test_float(float32x4_t p) {
  return __builtin_bit_cast(uint32x4_t,
    __builtin_neon_vceqzq_v(__builtin_bit_cast(int8x16_t, p), 41));
}

// Integer compare-with-zero: sext without negate
// LLVM-LABEL: define {{.*}} @_Z8test_int
// LLVM: icmp eq <4 x i32> {{%.*}}, zeroinitializer
// LLVM: sext <4 x i1> {{%.*}} to <4 x i32>
// LLVM-NOT: sub <4 x i32> zeroinitializer
// OGCG-LABEL: define {{.*}} @_Z8test_int
// OGCG: icmp eq <4 x i32> {{%.*}}, zeroinitializer
// OGCG: sext <4 x i1> {{%.*}} to <4 x i32>
uint32x4_t test_int(int32x4_t p) {
  return __builtin_bit_cast(uint32x4_t,
    __builtin_neon_vceqzq_v(__builtin_bit_cast(int8x16_t, p), 50));
}
