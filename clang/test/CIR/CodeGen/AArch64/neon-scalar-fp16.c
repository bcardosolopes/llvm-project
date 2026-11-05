// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_fp16.h>

// --- FP16 scalar absolute difference ---

// CIR-LABEL: @test_vabdh_f16
// CIR: call half @llvm.aarch64.sisd.fabd.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vabdh_f16
// OGCG: call half @llvm.aarch64.sisd.fabd.f16(half
// OGCG: ret half
float16_t test_vabdh_f16(float16_t a, float16_t b) {
  return vabdh_f16(a, b);
}

// --- FP16 scalar vcvta (round to nearest, ties away) ---

// CIR-LABEL: @test_vcvtah_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtas.i32.f16(half
// CIR: ret i32
// OGCG-LABEL: @test_vcvtah_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtas.i32.f16(half
// OGCG: ret i32
int32_t test_vcvtah_s32_f16(float16_t a) {
  return vcvtah_s32_f16(a);
}

// CIR-LABEL: @test_vcvtah_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtau.i64.f16(half
// CIR: ret i64
// OGCG-LABEL: @test_vcvtah_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtau.i64.f16(half
// OGCG: ret i64
uint64_t test_vcvtah_u64_f16(float16_t a) {
  return vcvtah_u64_f16(a);
}

// --- FP16 scalar vcvt (round toward zero) ---

// CIR-LABEL: @test_vcvth_s32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtzs.i32.f16(half
// CIR: ret i32
// OGCG-LABEL: @test_vcvth_s32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtzs.i32.f16(half
// OGCG: ret i32
int32_t test_vcvth_s32_f16(float16_t a) {
  return vcvth_s32_f16(a);
}

// --- FP16 scalar vcvtm (round toward -infinity) ---

// CIR-LABEL: @test_vcvtmh_u32_f16
// CIR: call i32 @llvm.aarch64.neon.fcvtmu.i32.f16(half
// CIR: ret i32
// OGCG-LABEL: @test_vcvtmh_u32_f16
// OGCG: call i32 @llvm.aarch64.neon.fcvtmu.i32.f16(half
// OGCG: ret i32
uint32_t test_vcvtmh_u32_f16(float16_t a) {
  return vcvtmh_u32_f16(a);
}

// --- FP16 scalar vcvtn (round to nearest even) ---

// CIR-LABEL: @test_vcvtnh_s64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtns.i64.f16(half
// CIR: ret i64
// OGCG-LABEL: @test_vcvtnh_s64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtns.i64.f16(half
// OGCG: ret i64
int64_t test_vcvtnh_s64_f16(float16_t a) {
  return vcvtnh_s64_f16(a);
}

// --- FP16 scalar vcvtp (round toward +infinity) ---

// CIR-LABEL: @test_vcvtph_u64_f16
// CIR: call i64 @llvm.aarch64.neon.fcvtpu.i64.f16(half
// CIR: ret i64
// OGCG-LABEL: @test_vcvtph_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.fcvtpu.i64.f16(half
// OGCG: ret i64
uint64_t test_vcvtph_u64_f16(float16_t a) {
  return vcvtph_u64_f16(a);
}

// --- FP16 fixed-point conversions ---

// CIR-LABEL: @test_vcvth_n_f16_s32
// CIR: call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i32(i32
// CIR: ret half
// OGCG-LABEL: @test_vcvth_n_f16_s32
// OGCG: call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i32(i32
// OGCG: ret half
float16_t test_vcvth_n_f16_s32(int32_t a) {
  return vcvth_n_f16_s32(a, 3);
}

// CIR-LABEL: @test_vcvth_n_u64_f16
// CIR: call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f16(half
// CIR: ret i64
// OGCG-LABEL: @test_vcvth_n_u64_f16
// OGCG: call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f16(half
// OGCG: ret i64
uint64_t test_vcvth_n_u64_f16(float16_t a) {
  return vcvth_n_u64_f16(a, 5);
}

// --- FP16 scalar multiply extended ---

// CIR-LABEL: @test_vmulxh_f16
// CIR: call half @llvm.aarch64.neon.fmulx.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vmulxh_f16
// OGCG: call half @llvm.aarch64.neon.fmulx.f16(half
// OGCG: ret half
float16_t test_vmulxh_f16(float16_t a, float16_t b) {
  return vmulxh_f16(a, b);
}

// --- FP16 scalar reciprocal estimate ---

// CIR-LABEL: @test_vrecpeh_f16
// CIR: call half @llvm.aarch64.neon.frecpe.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vrecpeh_f16
// OGCG: call half @llvm.aarch64.neon.frecpe.f16(half
// OGCG: ret half
float16_t test_vrecpeh_f16(float16_t a) {
  return vrecpeh_f16(a);
}

// --- FP16 scalar reciprocal exponent ---

// CIR-LABEL: @test_vrecpxh_f16
// CIR: call half @llvm.aarch64.neon.frecpx.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vrecpxh_f16
// OGCG: call half @llvm.aarch64.neon.frecpx.f16(half
// OGCG: ret half
float16_t test_vrecpxh_f16(float16_t a) {
  return vrecpxh_f16(a);
}

// --- FP16 scalar reciprocal square root estimate ---

// CIR-LABEL: @test_vrsqrteh_f16
// CIR: call half @llvm.aarch64.neon.frsqrte.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vrsqrteh_f16
// OGCG: call half @llvm.aarch64.neon.frsqrte.f16(half
// OGCG: ret half
float16_t test_vrsqrteh_f16(float16_t a) {
  return vrsqrteh_f16(a);
}

// --- FP16 scalar reciprocal square root step ---

// CIR-LABEL: @test_vrsqrtsh_f16
// CIR: call half @llvm.aarch64.neon.frsqrts.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vrsqrtsh_f16
// OGCG: call half @llvm.aarch64.neon.frsqrts.f16(half
// OGCG: ret half
float16_t test_vrsqrtsh_f16(float16_t a, float16_t b) {
  return vrsqrtsh_f16(a, b);
}
