// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +fullfp16 \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +fullfp16 \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_fp16.h>

// Ties-away rounding (vcvtah)

// CIR-LABEL: @test_vcvtah_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtas.i32.f16(half
// OGCG-LABEL: @test_vcvtah_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtas.i32.f16(half
int32_t test_vcvtah_s32_f16(float16_t a) { return vcvtah_s32_f16(a); }

// CIR-LABEL: @test_vcvtah_s64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtas.i64.f16(half
// OGCG-LABEL: @test_vcvtah_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtas.i64.f16(half
int64_t test_vcvtah_s64_f16(float16_t a) { return vcvtah_s64_f16(a); }

// CIR-LABEL: @test_vcvtah_u32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtau.i32.f16(half
// OGCG-LABEL: @test_vcvtah_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtau.i32.f16(half
uint32_t test_vcvtah_u32_f16(float16_t a) { return vcvtah_u32_f16(a); }

// CIR-LABEL: @test_vcvtah_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtau.i64.f16(half
// OGCG-LABEL: @test_vcvtah_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtau.i64.f16(half
uint64_t test_vcvtah_u64_f16(float16_t a) { return vcvtah_u64_f16(a); }

// Toward -infinity rounding (vcvtmh)

// CIR-LABEL: @test_vcvtmh_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtms.i32.f16(half
// OGCG-LABEL: @test_vcvtmh_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtms.i32.f16(half
int32_t test_vcvtmh_s32_f16(float16_t a) { return vcvtmh_s32_f16(a); }

// CIR-LABEL: @test_vcvtmh_s64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtms.i64.f16(half
// OGCG-LABEL: @test_vcvtmh_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtms.i64.f16(half
int64_t test_vcvtmh_s64_f16(float16_t a) { return vcvtmh_s64_f16(a); }

// CIR-LABEL: @test_vcvtmh_u32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtmu.i32.f16(half
// OGCG-LABEL: @test_vcvtmh_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtmu.i32.f16(half
uint32_t test_vcvtmh_u32_f16(float16_t a) { return vcvtmh_u32_f16(a); }

// CIR-LABEL: @test_vcvtmh_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtmu.i64.f16(half
// OGCG-LABEL: @test_vcvtmh_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtmu.i64.f16(half
uint64_t test_vcvtmh_u64_f16(float16_t a) { return vcvtmh_u64_f16(a); }

// Nearest even rounding (vcvtnh)

// CIR-LABEL: @test_vcvtnh_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtns.i32.f16(half
// OGCG-LABEL: @test_vcvtnh_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtns.i32.f16(half
int32_t test_vcvtnh_s32_f16(float16_t a) { return vcvtnh_s32_f16(a); }

// CIR-LABEL: @test_vcvtnh_s64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtns.i64.f16(half
// OGCG-LABEL: @test_vcvtnh_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtns.i64.f16(half
int64_t test_vcvtnh_s64_f16(float16_t a) { return vcvtnh_s64_f16(a); }

// CIR-LABEL: @test_vcvtnh_u32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtnu.i32.f16(half
// OGCG-LABEL: @test_vcvtnh_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtnu.i32.f16(half
uint32_t test_vcvtnh_u32_f16(float16_t a) { return vcvtnh_u32_f16(a); }

// CIR-LABEL: @test_vcvtnh_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtnu.i64.f16(half
// OGCG-LABEL: @test_vcvtnh_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtnu.i64.f16(half
uint64_t test_vcvtnh_u64_f16(float16_t a) { return vcvtnh_u64_f16(a); }

// Toward +infinity rounding (vcvtph)

// CIR-LABEL: @test_vcvtph_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtps.i32.f16(half
// OGCG-LABEL: @test_vcvtph_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtps.i32.f16(half
int32_t test_vcvtph_s32_f16(float16_t a) { return vcvtph_s32_f16(a); }

// CIR-LABEL: @test_vcvtph_s64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtps.i64.f16(half
// OGCG-LABEL: @test_vcvtph_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtps.i64.f16(half
int64_t test_vcvtph_s64_f16(float16_t a) { return vcvtph_s64_f16(a); }

// CIR-LABEL: @test_vcvtph_u32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtpu.i32.f16(half
// OGCG-LABEL: @test_vcvtph_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtpu.i32.f16(half
uint32_t test_vcvtph_u32_f16(float16_t a) { return vcvtph_u32_f16(a); }

// CIR-LABEL: @test_vcvtph_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtpu.i64.f16(half
// OGCG-LABEL: @test_vcvtph_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtpu.i64.f16(half
uint64_t test_vcvtph_u64_f16(float16_t a) { return vcvtph_u64_f16(a); }

// Toward zero truncation (vcvth)

// CIR-LABEL: @test_vcvth_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtzs.i32.f16(half
// OGCG-LABEL: @test_vcvth_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtzs.i32.f16(half
int32_t test_vcvth_s32_f16(float16_t a) { return vcvth_s32_f16(a); }

// CIR-LABEL: @test_vcvth_s64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtzs.i64.f16(half
// OGCG-LABEL: @test_vcvth_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtzs.i64.f16(half
int64_t test_vcvth_s64_f16(float16_t a) { return vcvth_s64_f16(a); }

// CIR-LABEL: @test_vcvth_u32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtzu.i32.f16(half
// OGCG-LABEL: @test_vcvth_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtzu.i32.f16(half
uint32_t test_vcvth_u32_f16(float16_t a) { return vcvth_u32_f16(a); }

// CIR-LABEL: @test_vcvth_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtzu.i64.f16(half
// OGCG-LABEL: @test_vcvth_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtzu.i64.f16(half
uint64_t test_vcvth_u64_f16(float16_t a) { return vcvth_u64_f16(a); }

// Fixed-point conversions (fp16)

// CIR-LABEL: @test_vcvth_n_f16_s32
// CIR: call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i32(i32 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_f16_s32
// OGCG: call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i32(i32 %{{.*}}, i32 3)
float16_t test_vcvth_n_f16_s32(int32_t a) { return vcvth_n_f16_s32(a, 3); }

// CIR-LABEL: @test_vcvth_n_f16_s64
// CIR: call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i64(i64 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_f16_s64
// OGCG: call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i64(i64 %{{.*}}, i32 3)
float16_t test_vcvth_n_f16_s64(int64_t a) { return vcvth_n_f16_s64(a, 3); }

// CIR-LABEL: @test_vcvth_n_f16_u32
// CIR: call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i32(i32 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_f16_u32
// OGCG: call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i32(i32 %{{.*}}, i32 3)
float16_t test_vcvth_n_f16_u32(uint32_t a) { return vcvth_n_f16_u32(a, 3); }

// CIR-LABEL: @test_vcvth_n_f16_u64
// CIR: call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i64(i64 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_f16_u64
// OGCG: call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i64(i64 %{{.*}}, i32 3)
float16_t test_vcvth_n_f16_u64(uint64_t a) { return vcvth_n_f16_u64(a, 3); }

// CIR-LABEL: @test_vcvth_n_s32_f16
// CIR: call i32 @llvm.aarch64.neon.vcvtfp2fxs.i32.f16(half %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.vcvtfp2fxs.i32.f16(half %{{.*}}, i32 3)
int32_t test_vcvth_n_s32_f16(float16_t a) { return vcvth_n_s32_f16(a, 3); }

// CIR-LABEL: @test_vcvth_n_s64_f16
// CIR: call i64 @llvm.aarch64.neon.vcvtfp2fxs.i64.f16(half %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.vcvtfp2fxs.i64.f16(half %{{.*}}, i32 3)
int64_t test_vcvth_n_s64_f16(float16_t a) { return vcvth_n_s64_f16(a, 3); }

// CIR-LABEL: @test_vcvth_n_u32_f16
// CIR: call i32 @llvm.aarch64.neon.vcvtfp2fxu.i32.f16(half %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.vcvtfp2fxu.i32.f16(half %{{.*}}, i32 3)
uint32_t test_vcvth_n_u32_f16(float16_t a) { return vcvth_n_u32_f16(a, 3); }

// CIR-LABEL: @test_vcvth_n_u64_f16
// CIR: call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f16(half %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvth_n_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f16(half %{{.*}}, i32 3)
uint64_t test_vcvth_n_u64_f16(float16_t a) { return vcvth_n_u64_f16(a, 3); }
