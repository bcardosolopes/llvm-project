// REQUIRES: aarch64-registered-target
// Tests for narrowing intrinsics (raddhn, rsubhn) that go through the generic
// fallthrough path in emitCommonNeonBuiltinExpr with Add1ArgType modifier.

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// --- vraddhn: rounding add and narrow high ---

// CIR-LABEL: @test_vraddhn_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.raddhn.v8i8(<8 x i16>
// OGCG-LABEL: @test_vraddhn_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.raddhn.v8i8(<8 x i16>
int8x8_t test_vraddhn_s16(int16x8_t a, int16x8_t b) {
  return vraddhn_s16(a, b);
}

// CIR-LABEL: @test_vraddhn_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.raddhn.v4i16(<4 x i32>
// OGCG-LABEL: @test_vraddhn_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.raddhn.v4i16(<4 x i32>
int16x4_t test_vraddhn_s32(int32x4_t a, int32x4_t b) {
  return vraddhn_s32(a, b);
}

// CIR-LABEL: @test_vraddhn_s64
// CIR: call <2 x i32> @llvm.aarch64.neon.raddhn.v2i32(<2 x i64>
// OGCG-LABEL: @test_vraddhn_s64
// OGCG: call <2 x i32> @llvm.aarch64.neon.raddhn.v2i32(<2 x i64>
int32x2_t test_vraddhn_s64(int64x2_t a, int64x2_t b) {
  return vraddhn_s64(a, b);
}

// CIR-LABEL: @test_vraddhn_u16
// CIR: call <8 x i8> @llvm.aarch64.neon.raddhn.v8i8(<8 x i16>
// OGCG-LABEL: @test_vraddhn_u16
// OGCG: call <8 x i8> @llvm.aarch64.neon.raddhn.v8i8(<8 x i16>
uint8x8_t test_vraddhn_u16(uint16x8_t a, uint16x8_t b) {
  return vraddhn_u16(a, b);
}

// --- vrsubhn: rounding subtract and narrow high ---

// CIR-LABEL: @test_vrsubhn_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.rsubhn.v8i8(<8 x i16>
// OGCG-LABEL: @test_vrsubhn_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.rsubhn.v8i8(<8 x i16>
int8x8_t test_vrsubhn_s16(int16x8_t a, int16x8_t b) {
  return vrsubhn_s16(a, b);
}

// CIR-LABEL: @test_vrsubhn_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.rsubhn.v4i16(<4 x i32>
// OGCG-LABEL: @test_vrsubhn_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.rsubhn.v4i16(<4 x i32>
int16x4_t test_vrsubhn_s32(int32x4_t a, int32x4_t b) {
  return vrsubhn_s32(a, b);
}

// CIR-LABEL: @test_vrsubhn_u16
// CIR: call <8 x i8> @llvm.aarch64.neon.rsubhn.v8i8(<8 x i16>
// OGCG-LABEL: @test_vrsubhn_u16
// OGCG: call <8 x i8> @llvm.aarch64.neon.rsubhn.v8i8(<8 x i16>
uint8x8_t test_vrsubhn_u16(uint16x8_t a, uint16x8_t b) {
  return vrsubhn_u16(a, b);
}

// --- vmul_p8: polynomial multiply (Add1ArgType but NOT narrowing) ---

// CIR-LABEL: @test_vmul_p8
// CIR: call <8 x i8> @llvm.aarch64.neon.pmul.v8i8(<8 x i8>
// OGCG-LABEL: @test_vmul_p8
// OGCG: call <8 x i8> @llvm.aarch64.neon.pmul.v8i8(<8 x i8>
poly8x8_t test_vmul_p8(poly8x8_t a, poly8x8_t b) {
  return vmul_p8(a, b);
}

// CIR-LABEL: @test_vmulq_p8
// CIR: call <16 x i8> @llvm.aarch64.neon.pmul.v16i8(<16 x i8>
// OGCG-LABEL: @test_vmulq_p8
// OGCG: call <16 x i8> @llvm.aarch64.neon.pmul.v16i8(<16 x i8>
poly8x16_t test_vmulq_p8(poly8x16_t a, poly8x16_t b) {
  return vmulq_p8(a, b);
}

// --- vqshl: saturating shift left (Add1ArgType, NOT narrowing, wider element) ---

// CIR-LABEL: @test_vqshl_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqshl_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshl.v4i16(<4 x i16>
int16x4_t test_vqshl_s16(int16x4_t a, int16x4_t b) {
  return vqshl_s16(a, b);
}

// CIR-LABEL: @test_vqshlq_u32
// CIR: call <4 x i32> @llvm.aarch64.neon.uqshl.v4i32(<4 x i32>
// OGCG-LABEL: @test_vqshlq_u32
// OGCG: call <4 x i32> @llvm.aarch64.neon.uqshl.v4i32(<4 x i32>
uint32x4_t test_vqshlq_u32(uint32x4_t a, uint32x4_t b) {
  return vqshlq_u32(a, b);
}
