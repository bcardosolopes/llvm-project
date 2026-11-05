// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -target-feature +aes -target-feature +sha3 -fclangir \
// RUN:    -disable-O0-optnone -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -target-feature +aes -target-feature +sha3 -fclangir \
// RUN:    -disable-O0-optnone -fno-clangir-call-conv-lowering \
// RUN:    -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -target-feature +aes -target-feature +sha3 \
// RUN:    -disable-O0-optnone -emit-llvm -o %t-og.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-og.ll %s

// Test crypto-related NEON builtins that require +aes and +sha3

typedef unsigned long long poly64_t;
typedef __uint128_t poly128_t;

// CIR-LABEL: @test_vmull_p64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.pmull64"
// CIR: cir.cast bitcast

// LLVM-LABEL: @test_vmull_p64
// LLVM: %{{.+}} = call <16 x i8> @llvm.aarch64.neon.pmull64(i64 %{{.+}}, i64 %{{.+}})
// LLVM: %{{.+}} = bitcast <16 x i8> %{{.+}} to i128

// OGCG-LABEL: @test_vmull_p64
// OGCG: %{{.+}} = call <16 x i8> @llvm.aarch64.neon.pmull64(i64 %{{.+}}, i64 %{{.+}})
// OGCG: %{{.+}} = bitcast <16 x i8> %{{.+}} to i128
poly128_t test_vmull_p64(poly64_t a, poly64_t b) {
  return __builtin_neon_vmull_p64(a, b);
}

// AES builtins

typedef __attribute__((neon_vector_type(16))) unsigned char uint8x16_t;
typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;
typedef __attribute__((neon_vector_type(4))) unsigned int uint32x4_t;
typedef __attribute__((neon_vector_type(2))) unsigned long long uint64x2_t;

// CIR-LABEL: @test_vaeseq_u8
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.aese"
//
// LLVM-LABEL: @test_vaeseq_u8
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aese(<16 x i8> %{{.+}}, <16 x i8> %{{.+}})
//
// OGCG-LABEL: @test_vaeseq_u8
// OGCG: call <16 x i8> @llvm.aarch64.crypto.aese(<16 x i8> %{{.+}}, <16 x i8> %{{.+}})
uint8x16_t test_vaeseq_u8(uint8x16_t data, uint8x16_t key) {
  return __builtin_neon_vaeseq_u8(data, key, 48);
}

// CIR-LABEL: @test_vaesdq_u8
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.aesd"
//
// LLVM-LABEL: @test_vaesdq_u8
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aesd(<16 x i8> %{{.+}}, <16 x i8> %{{.+}})
//
// OGCG-LABEL: @test_vaesdq_u8
// OGCG: call <16 x i8> @llvm.aarch64.crypto.aesd(<16 x i8> %{{.+}}, <16 x i8> %{{.+}})
uint8x16_t test_vaesdq_u8(uint8x16_t data, uint8x16_t key) {
  return __builtin_neon_vaesdq_u8(data, key, 48);
}

// CIR-LABEL: @test_vaesmcq_u8
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.aesmc"
//
// LLVM-LABEL: @test_vaesmcq_u8
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aesmc(<16 x i8> %{{.+}})
//
// OGCG-LABEL: @test_vaesmcq_u8
// OGCG: call <16 x i8> @llvm.aarch64.crypto.aesmc(<16 x i8> %{{.+}})
uint8x16_t test_vaesmcq_u8(uint8x16_t data) {
  return __builtin_neon_vaesmcq_u8(data, 48);
}

// CIR-LABEL: @test_vaesimcq_u8
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.aesimc"
//
// LLVM-LABEL: @test_vaesimcq_u8
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aesimc(<16 x i8> %{{.+}})
//
// OGCG-LABEL: @test_vaesimcq_u8
// OGCG: call <16 x i8> @llvm.aarch64.crypto.aesimc(<16 x i8> %{{.+}})
uint8x16_t test_vaesimcq_u8(uint8x16_t data) {
  return __builtin_neon_vaesimcq_u8(data, 48);
}

// SHA3 extension: veor3q (3-way exclusive OR)

// CIR-LABEL: @test_veor3q_u8
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.eor3u"
//
// LLVM-LABEL: @test_veor3q_u8
// LLVM: call <16 x i8> @llvm.aarch64.crypto.eor3u.v16i8(<16 x i8> %{{.+}}, <16 x i8> %{{.+}}, <16 x i8> %{{.+}})
//
// OGCG-LABEL: @test_veor3q_u8
// OGCG: call <16 x i8> @llvm.aarch64.crypto.eor3u.v16i8(<16 x i8> %{{.+}}, <16 x i8> %{{.+}}, <16 x i8> %{{.+}})
uint8x16_t test_veor3q_u8(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
  return __builtin_neon_veor3q_u8(a, b, c, 48);
}

// CIR-LABEL: @test_veor3q_s8
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.eor3s"
//
// LLVM-LABEL: @test_veor3q_s8
// LLVM: call <16 x i8> @llvm.aarch64.crypto.eor3s.v16i8(<16 x i8> %{{.+}}, <16 x i8> %{{.+}}, <16 x i8> %{{.+}})
//
// OGCG-LABEL: @test_veor3q_s8
// OGCG: call <16 x i8> @llvm.aarch64.crypto.eor3s.v16i8(<16 x i8> %{{.+}}, <16 x i8> %{{.+}}, <16 x i8> %{{.+}})
int8x16_t test_veor3q_s8(int8x16_t a, int8x16_t b, int8x16_t c) {
  return __builtin_neon_veor3q_s8(a, b, c, 32);
}

// CIR-LABEL: @test_veor3q_u32
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.eor3u"
//
// LLVM-LABEL: @test_veor3q_u32
// LLVM: call <4 x i32> @llvm.aarch64.crypto.eor3u.v4i32(<4 x i32> %{{.+}}, <4 x i32> %{{.+}}, <4 x i32> %{{.+}})
//
// OGCG-LABEL: @test_veor3q_u32
// OGCG: call <4 x i32> @llvm.aarch64.crypto.eor3u.v4i32(<4 x i32> %{{.+}}, <4 x i32> %{{.+}}, <4 x i32> %{{.+}})
uint32x4_t test_veor3q_u32(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
  return __builtin_neon_veor3q_u32(a, b, c, 50);
}

// CIR-LABEL: @test_veor3q_u64
// CIR: cir.call_llvm_intrinsic "aarch64.crypto.eor3u"
//
// LLVM-LABEL: @test_veor3q_u64
// LLVM: call <2 x i64> @llvm.aarch64.crypto.eor3u.v2i64(<2 x i64> %{{.+}}, <2 x i64> %{{.+}}, <2 x i64> %{{.+}})
//
// OGCG-LABEL: @test_veor3q_u64
// OGCG: call <2 x i64> @llvm.aarch64.crypto.eor3u.v2i64(<2 x i64> %{{.+}}, <2 x i64> %{{.+}}, <2 x i64> %{{.+}})
uint64x2_t test_veor3q_u64(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
  return __builtin_neon_veor3q_u64(a, b, c, 51);
}
