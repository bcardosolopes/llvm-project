// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +neon -fclangir -emit-llvm -O1 -o - %s | FileCheck %s

// AArch32 NEON rounding shift-right by immediate (vrshr_n), saturating
// shift-left by immediate (vqshl_n / vqshlu_n), integer<->float vector
// convert (vcvt), and the i64-element Qreg store-lane (vst1q_lane_s64/u64).
// Previously NYI on 32-bit ARM (emitARMBuiltinExpr); now lowered to the same
// arm.neon.* intrinsics (shifts) and plain sitofp/uitofp/fptosi/fptoui casts
// (converts) as classic CodeGen. vqshlu uses the single arm.neon.vqshiftsu
// intrinsic (signed in, unsigned out) with no input sign change, unlike
// AArch64's sqshlu. The i64 Qreg store-lane shuffles the selected lane into a
// one-element <1 x i64> vector and stores it via arm.neon.vst1, matching
// classic (a plain extract + i64 store gives poor backend code).

#include <arm_neon.h>

// --- vrshr_n / vrshrq_n: signed -> vrshifts, unsigned -> vrshiftu ---

// CHECK-LABEL: @rshr_s8(
// CHECK: call <8 x i8> @llvm.arm.neon.vrshifts.v8i8(<8 x i8> %{{.*}}, <8 x i8> splat (i8 -3))
int8x8_t rshr_s8(int8x8_t a) { return vrshr_n_s8(a, 3); }

// CHECK-LABEL: @rshr_u8(
// CHECK: call <8 x i8> @llvm.arm.neon.vrshiftu.v8i8(<8 x i8> %{{.*}}, <8 x i8> splat (i8 -3))
uint8x8_t rshr_u8(uint8x8_t a) { return vrshr_n_u8(a, 3); }

// CHECK-LABEL: @rshr_s16(
// CHECK: call <4 x i16> @llvm.arm.neon.vrshifts.v4i16(
int16x4_t rshr_s16(int16x4_t a) { return vrshr_n_s16(a, 5); }

// CHECK-LABEL: @rshr_u32(
// CHECK: call <2 x i32> @llvm.arm.neon.vrshiftu.v2i32(
uint32x2_t rshr_u32(uint32x2_t a) { return vrshr_n_u32(a, 9); }

// CHECK-LABEL: @rshr_s64(
// CHECK: call <1 x i64> @llvm.arm.neon.vrshifts.v1i64(
int64x1_t rshr_s64(int64x1_t a) { return vrshr_n_s64(a, 17); }

// CHECK-LABEL: @rshrq_s32(
// CHECK: call <4 x i32> @llvm.arm.neon.vrshifts.v4i32(
int32x4_t rshrq_s32(int32x4_t a) { return vrshrq_n_s32(a, 9); }

// CHECK-LABEL: @rshrq_u64(
// CHECK: call <2 x i64> @llvm.arm.neon.vrshiftu.v2i64(
uint64x2_t rshrq_u64(uint64x2_t a) { return vrshrq_n_u64(a, 17); }

// --- vqshl_n / vqshlq_n: signed -> vqshifts, unsigned -> vqshiftu ---

// CHECK-LABEL: @qshl_s8(
// CHECK: call <8 x i8> @llvm.arm.neon.vqshifts.v8i8(<8 x i8> %{{.*}}, <8 x i8> splat (i8 3))
int8x8_t qshl_s8(int8x8_t a) { return vqshl_n_s8(a, 3); }

// CHECK-LABEL: @qshl_u16(
// CHECK: call <4 x i16> @llvm.arm.neon.vqshiftu.v4i16(
uint16x4_t qshl_u16(uint16x4_t a) { return vqshl_n_u16(a, 5); }

// CHECK-LABEL: @qshl_s64(
// CHECK: call <1 x i64> @llvm.arm.neon.vqshifts.v1i64(
int64x1_t qshl_s64(int64x1_t a) { return vqshl_n_s64(a, 17); }

// CHECK-LABEL: @qshlq_s32(
// CHECK: call <4 x i32> @llvm.arm.neon.vqshifts.v4i32(
int32x4_t qshlq_s32(int32x4_t a) { return vqshlq_n_s32(a, 9); }

// CHECK-LABEL: @qshlq_u8(
// CHECK: call <16 x i8> @llvm.arm.neon.vqshiftu.v16i8(
uint8x16_t qshlq_u8(uint8x16_t a) { return vqshlq_n_u8(a, 3); }

// --- vqshlu_n / vqshluq_n: signed in, unsigned out -> vqshiftsu ---

// CHECK-LABEL: @qshlu_s8(
// CHECK: call <8 x i8> @llvm.arm.neon.vqshiftsu.v8i8(<8 x i8> %{{.*}}, <8 x i8> splat (i8 3))
uint8x8_t qshlu_s8(int8x8_t a) { return vqshlu_n_s8(a, 3); }

// CHECK-LABEL: @qshlu_s32(
// CHECK: call <2 x i32> @llvm.arm.neon.vqshiftsu.v2i32(
uint32x2_t qshlu_s32(int32x2_t a) { return vqshlu_n_s32(a, 9); }

// CHECK-LABEL: @qshlu_s64(
// CHECK: call <1 x i64> @llvm.arm.neon.vqshiftsu.v1i64(
uint64x1_t qshlu_s64(int64x1_t a) { return vqshlu_n_s64(a, 17); }

// CHECK-LABEL: @qshluq_s16(
// CHECK: call <8 x i16> @llvm.arm.neon.vqshiftsu.v8i16(
uint16x8_t qshluq_s16(int16x8_t a) { return vqshluq_n_s16(a, 5); }

// CHECK-LABEL: @qshluq_s32(
// CHECK: call <4 x i32> @llvm.arm.neon.vqshiftsu.v4i32(
uint32x4_t qshluq_s32(int32x4_t a) { return vqshluq_n_s32(a, 9); }

// --- vcvt int -> float (sitofp / uitofp), 32-bit only ---

// CHECK-LABEL: @cvt_f32_s32(
// CHECK: sitofp <2 x i32> %{{.*}} to <2 x float>
float32x2_t cvt_f32_s32(int32x2_t a) { return vcvt_f32_s32(a); }

// CHECK-LABEL: @cvt_f32_u32(
// CHECK: uitofp <2 x i32> %{{.*}} to <2 x float>
float32x2_t cvt_f32_u32(uint32x2_t a) { return vcvt_f32_u32(a); }

// CHECK-LABEL: @cvtq_f32_s32(
// CHECK: sitofp <4 x i32> %{{.*}} to <4 x float>
float32x4_t cvtq_f32_s32(int32x4_t a) { return vcvtq_f32_s32(a); }

// CHECK-LABEL: @cvtq_f32_u32(
// CHECK: uitofp <4 x i32> %{{.*}} to <4 x float>
float32x4_t cvtq_f32_u32(uint32x4_t a) { return vcvtq_f32_u32(a); }

// --- vcvt float -> int (fptosi / fptoui), 32-bit only ---

// CHECK-LABEL: @cvt_s32_f32(
// CHECK: fptosi <2 x float> %{{.*}} to <2 x i32>
int32x2_t cvt_s32_f32(float32x2_t a) { return vcvt_s32_f32(a); }

// CHECK-LABEL: @cvt_u32_f32(
// CHECK: fptoui <2 x float> %{{.*}} to <2 x i32>
uint32x2_t cvt_u32_f32(float32x2_t a) { return vcvt_u32_f32(a); }

// CHECK-LABEL: @cvtq_s32_f32(
// CHECK: fptosi <4 x float> %{{.*}} to <4 x i32>
int32x4_t cvtq_s32_f32(float32x4_t a) { return vcvtq_s32_f32(a); }

// CHECK-LABEL: @cvtq_u32_f32(
// CHECK: fptoui <4 x float> %{{.*}} to <4 x i32>
uint32x4_t cvtq_u32_f32(float32x4_t a) { return vcvtq_u32_f32(a); }

// --- i64-element Qreg store-lane: shuffle lane -> <1 x i64> + arm.neon.vst1 ---

// CHECK-LABEL: @stq_lane_s64_0(
// CHECK: shufflevector <2 x i64> %{{.*}}, <2 x i64> {{.*}}, <1 x i32> zeroinitializer
// CHECK: call void @llvm.arm.neon.vst1.p0.v1i64(ptr %{{.*}}, <1 x i64> %{{.*}}, i32 8)
void stq_lane_s64_0(int64_t *p, int64x2_t v) { vst1q_lane_s64(p, v, 0); }

// CHECK-LABEL: @stq_lane_s64_1(
// CHECK: shufflevector <2 x i64> %{{.*}}, <2 x i64> {{.*}}, <1 x i32> <i32 1>
// CHECK: call void @llvm.arm.neon.vst1.p0.v1i64(ptr %{{.*}}, <1 x i64> %{{.*}}, i32 8)
void stq_lane_s64_1(int64_t *p, int64x2_t v) { vst1q_lane_s64(p, v, 1); }

// CHECK-LABEL: @stq_lane_u64_1(
// CHECK: shufflevector <2 x i64> %{{.*}}, <2 x i64> {{.*}}, <1 x i32> <i32 1>
// CHECK: call void @llvm.arm.neon.vst1.p0.v1i64(ptr %{{.*}}, <1 x i64> %{{.*}}, i32 8)
void stq_lane_u64_1(uint64_t *p, uint64x2_t v) { vst1q_lane_u64(p, v, 1); }

// Dreg i64 store-lane and a 32-bit Qreg store-lane still use plain
// extract + scalar store (regression guard for the non-i64-Qreg widths).

// CHECK-LABEL: @st_lane_s64_d(
// CHECK: extractelement <1 x i64> %{{.*}}, i64 0
// CHECK: store i64
void st_lane_s64_d(int64_t *p, int64x1_t v) { vst1_lane_s64(p, v, 0); }

// CHECK-LABEL: @stq_lane_s32(
// CHECK: extractelement <4 x i32> %{{.*}}, i64 2
// CHECK: store i32
void stq_lane_s32(int32_t *p, int32x4_t v) { vst1q_lane_s32(p, v, 2); }
