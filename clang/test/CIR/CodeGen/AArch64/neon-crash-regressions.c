// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

#include <arm_neon.h>

uint8x8_t test_vcnt_u8(uint8x8_t a) { return vcnt_u8(a); }
// CIR-LABEL: @test_vcnt_u8
// CIR: call <8 x i8> @llvm.ctpop.v8i8(<8 x i8>
// OGCG-LABEL: @test_vcnt_u8
// OGCG: call <8 x i8> @llvm.ctpop.v8i8(<8 x i8>

uint8x16_t test_vcntq_u8(uint8x16_t a) { return vcntq_u8(a); }
// CIR-LABEL: @test_vcntq_u8
// CIR: call <16 x i8> @llvm.ctpop.v16i8(<16 x i8>
// OGCG-LABEL: @test_vcntq_u8
// OGCG: call <16 x i8> @llvm.ctpop.v16i8(<16 x i8>

int16x8_t test_vclzq_s16(int16x8_t a) { return vclzq_s16(a); }
// CIR-LABEL: @test_vclzq_s16
// CIR: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> %{{.*}}, i1 false)
// OGCG-LABEL: @test_vclzq_s16
// OGCG: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> %{{.*}}, i1 false)

float16x4_t test_vcvt_f16_s16(int16x4_t a) { return vcvt_f16_s16(a); }
// CIR-LABEL: @test_vcvt_f16_s16
// CIR: sitofp <4 x i16>
// OGCG-LABEL: @test_vcvt_f16_s16
// OGCG: sitofp <4 x i16>

float16x4_t test_vcvt_f16_u16(uint16x4_t a) { return vcvt_f16_u16(a); }
// CIR-LABEL: @test_vcvt_f16_u16
// CIR: uitofp <4 x i16>
// OGCG-LABEL: @test_vcvt_f16_u16
// OGCG: uitofp <4 x i16>

float16x8_t test_vcvtq_f16_s16(int16x8_t a) { return vcvtq_f16_s16(a); }
// CIR-LABEL: @test_vcvtq_f16_s16
// CIR: sitofp <8 x i16>
// OGCG-LABEL: @test_vcvtq_f16_s16
// OGCG: sitofp <8 x i16>

float16x8_t test_vcvtq_f16_u16(uint16x8_t a) { return vcvtq_f16_u16(a); }
// CIR-LABEL: @test_vcvtq_f16_u16
// CIR: uitofp <8 x i16>
// OGCG-LABEL: @test_vcvtq_f16_u16
// OGCG: uitofp <8 x i16>

int16x4_t test_vsubhn_s32(int32x4_t a, int32x4_t b) { return vsubhn_s32(a, b); }
// CIR-LABEL: @test_vsubhn_s32
// CIR: sub <4 x i32>
// CIR: lshr <4 x i32>
// CIR: trunc <4 x i32>
// OGCG-LABEL: @test_vsubhn_s32
// OGCG: sub <4 x i32>
// OGCG: lshr <4 x i32>
// OGCG: trunc <4 x i32>

int64_t test_vpaddd_s64(int64x2_t a) { return vpaddd_s64(a); }
// CIR-LABEL: @test_vpaddd_s64
// CIR: call i64 @llvm.vector.reduce.add.v2i64(<2 x i64>
// OGCG-LABEL: @test_vpaddd_s64
// OGCG: call i64 @llvm.vector.reduce.add.v2i64(<2 x i64>

uint64_t test_vpaddd_u64(uint64x2_t a) { return vpaddd_u64(a); }
// CIR-LABEL: @test_vpaddd_u64
// CIR: call i64 @llvm.vector.reduce.add.v2i64(<2 x i64>
// OGCG-LABEL: @test_vpaddd_u64
// OGCG: call i64 @llvm.vector.reduce.add.v2i64(<2 x i64>

float32_t test_vpadds_f32(float32x2_t a) { return vpadds_f32(a); }
// CIR-LABEL: @test_vpadds_f32
// CIR: extractelement <2 x float>
// CIR: extractelement <2 x float>
// CIR: fadd float
// OGCG-LABEL: @test_vpadds_f32
// OGCG: extractelement <2 x float>
// OGCG: extractelement <2 x float>
// OGCG: fadd float

float64_t test_vpaddd_f64(float64x2_t a) { return vpaddd_f64(a); }
// CIR-LABEL: @test_vpaddd_f64
// CIR: extractelement <2 x double>
// CIR: extractelement <2 x double>
// CIR: fadd double
// OGCG-LABEL: @test_vpaddd_f64
// OGCG: extractelement <2 x double>
// OGCG: extractelement <2 x double>
// OGCG: fadd double
