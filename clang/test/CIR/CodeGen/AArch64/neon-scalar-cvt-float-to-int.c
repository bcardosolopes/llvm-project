// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// Ties-away rounding (vcvta)

// CIR-LABEL: @test_vcvtad_s64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtas.i64.f64(double
// OGCG-LABEL: @test_vcvtad_s64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtas.i64.f64(double
int64_t test_vcvtad_s64_f64(float64_t a) { return vcvtad_s64_f64(a); }

// CIR-LABEL: @test_vcvtad_u64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtau.i64.f64(double
// OGCG-LABEL: @test_vcvtad_u64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtau.i64.f64(double
uint64_t test_vcvtad_u64_f64(float64_t a) { return vcvtad_u64_f64(a); }

// CIR-LABEL: @test_vcvtas_s32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtas.i32.f32(float
// OGCG-LABEL: @test_vcvtas_s32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtas.i32.f32(float
int32_t test_vcvtas_s32_f32(float32_t a) { return vcvtas_s32_f32(a); }

// CIR-LABEL: @test_vcvtas_u32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtau.i32.f32(float
// OGCG-LABEL: @test_vcvtas_u32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtau.i32.f32(float
uint32_t test_vcvtas_u32_f32(float32_t a) { return vcvtas_u32_f32(a); }

// Toward -infinity rounding (vcvtm)

// CIR-LABEL: @test_vcvtmd_s64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtms.i64.f64(double
// OGCG-LABEL: @test_vcvtmd_s64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtms.i64.f64(double
int64_t test_vcvtmd_s64_f64(float64_t a) { return vcvtmd_s64_f64(a); }

// CIR-LABEL: @test_vcvtmd_u64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtmu.i64.f64(double
// OGCG-LABEL: @test_vcvtmd_u64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtmu.i64.f64(double
uint64_t test_vcvtmd_u64_f64(float64_t a) { return vcvtmd_u64_f64(a); }

// CIR-LABEL: @test_vcvtms_s32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtms.i32.f32(float
// OGCG-LABEL: @test_vcvtms_s32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtms.i32.f32(float
int32_t test_vcvtms_s32_f32(float32_t a) { return vcvtms_s32_f32(a); }

// CIR-LABEL: @test_vcvtms_u32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtmu.i32.f32(float
// OGCG-LABEL: @test_vcvtms_u32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtmu.i32.f32(float
uint32_t test_vcvtms_u32_f32(float32_t a) { return vcvtms_u32_f32(a); }

// Nearest even rounding (vcvtn)

// CIR-LABEL: @test_vcvtnd_s64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtns.i64.f64(double
// OGCG-LABEL: @test_vcvtnd_s64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtns.i64.f64(double
int64_t test_vcvtnd_s64_f64(float64_t a) { return vcvtnd_s64_f64(a); }

// CIR-LABEL: @test_vcvtnd_u64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtnu.i64.f64(double
// OGCG-LABEL: @test_vcvtnd_u64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtnu.i64.f64(double
uint64_t test_vcvtnd_u64_f64(float64_t a) { return vcvtnd_u64_f64(a); }

// CIR-LABEL: @test_vcvtns_s32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtns.i32.f32(float
// OGCG-LABEL: @test_vcvtns_s32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtns.i32.f32(float
int32_t test_vcvtns_s32_f32(float32_t a) { return vcvtns_s32_f32(a); }

// CIR-LABEL: @test_vcvtns_u32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtnu.i32.f32(float
// OGCG-LABEL: @test_vcvtns_u32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtnu.i32.f32(float
uint32_t test_vcvtns_u32_f32(float32_t a) { return vcvtns_u32_f32(a); }

// Toward +infinity rounding (vcvtp)

// CIR-LABEL: @test_vcvtpd_s64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtps.i64.f64(double
// OGCG-LABEL: @test_vcvtpd_s64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtps.i64.f64(double
int64_t test_vcvtpd_s64_f64(float64_t a) { return vcvtpd_s64_f64(a); }

// CIR-LABEL: @test_vcvtpd_u64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtpu.i64.f64(double
// OGCG-LABEL: @test_vcvtpd_u64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtpu.i64.f64(double
uint64_t test_vcvtpd_u64_f64(float64_t a) { return vcvtpd_u64_f64(a); }

// CIR-LABEL: @test_vcvtps_s32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtps.i32.f32(float
// OGCG-LABEL: @test_vcvtps_s32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtps.i32.f32(float
int32_t test_vcvtps_s32_f32(float32_t a) { return vcvtps_s32_f32(a); }

// CIR-LABEL: @test_vcvtps_u32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtpu.i32.f32(float
// OGCG-LABEL: @test_vcvtps_u32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtpu.i32.f32(float
uint32_t test_vcvtps_u32_f32(float32_t a) { return vcvtps_u32_f32(a); }

// Toward zero truncation (vcvt)

// CIR-LABEL: @test_vcvtd_s64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtzs.i64.f64(double
// OGCG-LABEL: @test_vcvtd_s64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtzs.i64.f64(double
int64_t test_vcvtd_s64_f64(float64_t a) { return vcvtd_s64_f64(a); }

// CIR-LABEL: @test_vcvtd_u64_f64
// CIR: call i64 @llvm.aarch64.neon.fcvtzu.i64.f64(double
// OGCG-LABEL: @test_vcvtd_u64_f64
// OGCG: call i64 @llvm.aarch64.neon.fcvtzu.i64.f64(double
uint64_t test_vcvtd_u64_f64(float64_t a) { return vcvtd_u64_f64(a); }

// CIR-LABEL: @test_vcvts_s32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtzs.i32.f32(float
// OGCG-LABEL: @test_vcvts_s32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtzs.i32.f32(float
int32_t test_vcvts_s32_f32(float32_t a) { return vcvts_s32_f32(a); }

// CIR-LABEL: @test_vcvts_u32_f32
// CIR: call i32 @llvm.aarch64.neon.fcvtzu.i32.f32(float
// OGCG-LABEL: @test_vcvts_u32_f32
// OGCG: call i32 @llvm.aarch64.neon.fcvtzu.i32.f32(float
uint32_t test_vcvts_u32_f32(float32_t a) { return vcvts_u32_f32(a); }

// Fixed-point to floating-point conversions

// CIR-LABEL: @test_vcvtd_n_f64_s64
// CIR: call double @llvm.aarch64.neon.vcvtfxs2fp.f64.i64(i64 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtd_n_f64_s64
// OGCG: call double @llvm.aarch64.neon.vcvtfxs2fp.f64.i64(i64 %{{.*}}, i32 3)
float64_t test_vcvtd_n_f64_s64(int64_t a) { return vcvtd_n_f64_s64(a, 3); }

// CIR-LABEL: @test_vcvtd_n_f64_u64
// CIR: call double @llvm.aarch64.neon.vcvtfxu2fp.f64.i64(i64 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtd_n_f64_u64
// OGCG: call double @llvm.aarch64.neon.vcvtfxu2fp.f64.i64(i64 %{{.*}}, i32 3)
float64_t test_vcvtd_n_f64_u64(uint64_t a) { return vcvtd_n_f64_u64(a, 3); }

// CIR-LABEL: @test_vcvts_n_f32_s32
// CIR: call float @llvm.aarch64.neon.vcvtfxs2fp.f32.i32(i32 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvts_n_f32_s32
// OGCG: call float @llvm.aarch64.neon.vcvtfxs2fp.f32.i32(i32 %{{.*}}, i32 3)
float32_t test_vcvts_n_f32_s32(int32_t a) { return vcvts_n_f32_s32(a, 3); }

// CIR-LABEL: @test_vcvts_n_f32_u32
// CIR: call float @llvm.aarch64.neon.vcvtfxu2fp.f32.i32(i32 %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvts_n_f32_u32
// OGCG: call float @llvm.aarch64.neon.vcvtfxu2fp.f32.i32(i32 %{{.*}}, i32 3)
float32_t test_vcvts_n_f32_u32(uint32_t a) { return vcvts_n_f32_u32(a, 3); }

// Floating-point to fixed-point conversions

// CIR-LABEL: @test_vcvtd_n_s64_f64
// CIR: call i64 @llvm.aarch64.neon.vcvtfp2fxs.i64.f64(double %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtd_n_s64_f64
// OGCG: call i64 @llvm.aarch64.neon.vcvtfp2fxs.i64.f64(double %{{.*}}, i32 3)
int64_t test_vcvtd_n_s64_f64(float64_t a) { return vcvtd_n_s64_f64(a, 3); }

// CIR-LABEL: @test_vcvtd_n_u64_f64
// CIR: call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f64(double %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtd_n_u64_f64
// OGCG: call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f64(double %{{.*}}, i32 3)
uint64_t test_vcvtd_n_u64_f64(float64_t a) { return vcvtd_n_u64_f64(a, 3); }

// CIR-LABEL: @test_vcvts_n_s32_f32
// CIR: call i32 @llvm.aarch64.neon.vcvtfp2fxs.i32.f32(float %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvts_n_s32_f32
// OGCG: call i32 @llvm.aarch64.neon.vcvtfp2fxs.i32.f32(float %{{.*}}, i32 3)
int32_t test_vcvts_n_s32_f32(float32_t a) { return vcvts_n_s32_f32(a, 3); }

// CIR-LABEL: @test_vcvts_n_u32_f32
// CIR: call i32 @llvm.aarch64.neon.vcvtfp2fxu.i32.f32(float %{{.*}}, i32 3)
// OGCG-LABEL: @test_vcvts_n_u32_f32
// OGCG: call i32 @llvm.aarch64.neon.vcvtfp2fxu.i32.f32(float %{{.*}}, i32 3)
uint32_t test_vcvts_n_u32_f32(float32_t a) { return vcvts_n_u32_f32(a, 3); }

// Narrow float64 to float32 (vcvtxd)

// CIR-LABEL: @test_vcvtxd_f32_f64
// CIR: call float @llvm.aarch64.sisd.fcvtxn(double
// OGCG-LABEL: @test_vcvtxd_f32_f64
// OGCG: call float @llvm.aarch64.sisd.fcvtxn(double
float32_t test_vcvtxd_f32_f64(float64_t a) { return vcvtxd_f32_f64(a); }

// Cross-size conversions: f64 -> i32 (ties-away)

// CIR-LABEL: @test_vcvtad_s32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtas.i32.f64(double
// OGCG-LABEL: @test_vcvtad_s32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtas.i32.f64(double
int32_t test_vcvtad_s32_f64(float64_t a) { return vcvtad_s32_f64(a); }

// CIR-LABEL: @test_vcvtad_u32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtau.i32.f64(double
// OGCG-LABEL: @test_vcvtad_u32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtau.i32.f64(double
uint32_t test_vcvtad_u32_f64(float64_t a) { return vcvtad_u32_f64(a); }

// Cross-size conversions: f32 -> i64 (ties-away)

// CIR-LABEL: @test_vcvtas_s64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtas.i64.f32(float
// OGCG-LABEL: @test_vcvtas_s64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtas.i64.f32(float
int64_t test_vcvtas_s64_f32(float32_t a) { return vcvtas_s64_f32(a); }

// CIR-LABEL: @test_vcvtas_u64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtau.i64.f32(float
// OGCG-LABEL: @test_vcvtas_u64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtau.i64.f32(float
uint64_t test_vcvtas_u64_f32(float32_t a) { return vcvtas_u64_f32(a); }

// Cross-size conversions: f64 -> i32 (toward -infinity)

// CIR-LABEL: @test_vcvtmd_s32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtms.i32.f64(double
// OGCG-LABEL: @test_vcvtmd_s32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtms.i32.f64(double
int32_t test_vcvtmd_s32_f64(float64_t a) { return vcvtmd_s32_f64(a); }

// CIR-LABEL: @test_vcvtmd_u32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtmu.i32.f64(double
// OGCG-LABEL: @test_vcvtmd_u32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtmu.i32.f64(double
uint32_t test_vcvtmd_u32_f64(float64_t a) { return vcvtmd_u32_f64(a); }

// Cross-size conversions: f32 -> i64 (toward -infinity)

// CIR-LABEL: @test_vcvtms_s64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtms.i64.f32(float
// OGCG-LABEL: @test_vcvtms_s64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtms.i64.f32(float
int64_t test_vcvtms_s64_f32(float32_t a) { return vcvtms_s64_f32(a); }

// CIR-LABEL: @test_vcvtms_u64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtmu.i64.f32(float
// OGCG-LABEL: @test_vcvtms_u64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtmu.i64.f32(float
uint64_t test_vcvtms_u64_f32(float32_t a) { return vcvtms_u64_f32(a); }

// Cross-size conversions: f64 -> i32 (nearest even)

// CIR-LABEL: @test_vcvtnd_s32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtns.i32.f64(double
// OGCG-LABEL: @test_vcvtnd_s32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtns.i32.f64(double
int32_t test_vcvtnd_s32_f64(float64_t a) { return vcvtnd_s32_f64(a); }

// CIR-LABEL: @test_vcvtnd_u32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtnu.i32.f64(double
// OGCG-LABEL: @test_vcvtnd_u32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtnu.i32.f64(double
uint32_t test_vcvtnd_u32_f64(float64_t a) { return vcvtnd_u32_f64(a); }

// Cross-size conversions: f32 -> i64 (nearest even)

// CIR-LABEL: @test_vcvtns_s64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtns.i64.f32(float
// OGCG-LABEL: @test_vcvtns_s64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtns.i64.f32(float
int64_t test_vcvtns_s64_f32(float32_t a) { return vcvtns_s64_f32(a); }

// CIR-LABEL: @test_vcvtns_u64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtnu.i64.f32(float
// OGCG-LABEL: @test_vcvtns_u64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtnu.i64.f32(float
uint64_t test_vcvtns_u64_f32(float32_t a) { return vcvtns_u64_f32(a); }

// Cross-size conversions: f64 -> i32 (toward +infinity)

// CIR-LABEL: @test_vcvtpd_s32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtps.i32.f64(double
// OGCG-LABEL: @test_vcvtpd_s32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtps.i32.f64(double
int32_t test_vcvtpd_s32_f64(float64_t a) { return vcvtpd_s32_f64(a); }

// CIR-LABEL: @test_vcvtpd_u32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtpu.i32.f64(double
// OGCG-LABEL: @test_vcvtpd_u32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtpu.i32.f64(double
uint32_t test_vcvtpd_u32_f64(float64_t a) { return vcvtpd_u32_f64(a); }

// Cross-size conversions: f32 -> i64 (toward +infinity)

// CIR-LABEL: @test_vcvtps_s64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtps.i64.f32(float
// OGCG-LABEL: @test_vcvtps_s64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtps.i64.f32(float
int64_t test_vcvtps_s64_f32(float32_t a) { return vcvtps_s64_f32(a); }

// CIR-LABEL: @test_vcvtps_u64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtpu.i64.f32(float
// OGCG-LABEL: @test_vcvtps_u64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtpu.i64.f32(float
uint64_t test_vcvtps_u64_f32(float32_t a) { return vcvtps_u64_f32(a); }

// Cross-size conversions: f64 -> i32 (toward zero)

// CIR-LABEL: @test_vcvtd_s32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtzs.i32.f64(double
// OGCG-LABEL: @test_vcvtd_s32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtzs.i32.f64(double
int32_t test_vcvtd_s32_f64(float64_t a) { return vcvtd_s32_f64(a); }

// CIR-LABEL: @test_vcvtd_u32_f64
// CIR: call i32 @llvm.aarch64.neon.fcvtzu.i32.f64(double
// OGCG-LABEL: @test_vcvtd_u32_f64
// OGCG: call i32 @llvm.aarch64.neon.fcvtzu.i32.f64(double
uint32_t test_vcvtd_u32_f64(float64_t a) { return vcvtd_u32_f64(a); }

// Cross-size conversions: f32 -> i64 (toward zero)

// CIR-LABEL: @test_vcvts_s64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtzs.i64.f32(float
// OGCG-LABEL: @test_vcvts_s64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtzs.i64.f32(float
int64_t test_vcvts_s64_f32(float32_t a) { return vcvts_s64_f32(a); }

// CIR-LABEL: @test_vcvts_u64_f32
// CIR: call i64 @llvm.aarch64.neon.fcvtzu.i64.f32(float
// OGCG-LABEL: @test_vcvts_u64_f32
// OGCG: call i64 @llvm.aarch64.neon.fcvtzu.i64.f32(float
uint64_t test_vcvts_u64_f32(float32_t a) { return vcvts_u64_f32(a); }
