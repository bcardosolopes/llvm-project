// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// Ties-away rounding (vcvta)

// CIR-LABEL: @test_vcvtaq_s32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtas.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtaq_s32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtas.v4i32.v4f32(<4 x float>
int32x4_t test_vcvtaq_s32_f32(float32x4_t a) { return vcvtaq_s32_f32(a); }

// CIR-LABEL: @test_vcvtaq_u32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtau.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtaq_u32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtau.v4i32.v4f32(<4 x float>
uint32x4_t test_vcvtaq_u32_f32(float32x4_t a) { return vcvtaq_u32_f32(a); }

// Toward -infinity rounding (vcvtm)

// CIR-LABEL: @test_vcvtmq_s32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtms.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtmq_s32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtms.v4i32.v4f32(<4 x float>
int32x4_t test_vcvtmq_s32_f32(float32x4_t a) { return vcvtmq_s32_f32(a); }

// CIR-LABEL: @test_vcvtmq_u32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtmu.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtmq_u32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtmu.v4i32.v4f32(<4 x float>
uint32x4_t test_vcvtmq_u32_f32(float32x4_t a) { return vcvtmq_u32_f32(a); }

// Nearest-even rounding (vcvtn)

// CIR-LABEL: @test_vcvtnq_s32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtns.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtnq_s32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtns.v4i32.v4f32(<4 x float>
int32x4_t test_vcvtnq_s32_f32(float32x4_t a) { return vcvtnq_s32_f32(a); }

// CIR-LABEL: @test_vcvtnq_u32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtnu.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtnq_u32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtnu.v4i32.v4f32(<4 x float>
uint32x4_t test_vcvtnq_u32_f32(float32x4_t a) { return vcvtnq_u32_f32(a); }

// Toward +infinity rounding (vcvtp)

// CIR-LABEL: @test_vcvtpq_s32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtps.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtpq_s32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtps.v4i32.v4f32(<4 x float>
int32x4_t test_vcvtpq_s32_f32(float32x4_t a) { return vcvtpq_s32_f32(a); }

// CIR-LABEL: @test_vcvtpq_u32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtpu.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtpq_u32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtpu.v4i32.v4f32(<4 x float>
uint32x4_t test_vcvtpq_u32_f32(float32x4_t a) { return vcvtpq_u32_f32(a); }

// Toward zero (vcvt)

// CIR-LABEL: @test_vcvtq_s32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtzs.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtq_s32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtzs.v4i32.v4f32(<4 x float>
int32x4_t test_vcvtq_s32_f32(float32x4_t a) { return vcvtq_s32_f32(a); }

// CIR-LABEL: @test_vcvtq_u32_f32
// CIR: call <4 x i32> @llvm.aarch64.neon.fcvtzu.v4i32.v4f32(<4 x float>
// OGCG-LABEL: @test_vcvtq_u32_f32
// OGCG: call <4 x i32> @llvm.aarch64.neon.fcvtzu.v4i32.v4f32(<4 x float>
uint32x4_t test_vcvtq_u32_f32(float32x4_t a) { return vcvtq_u32_f32(a); }
