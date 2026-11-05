// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fp-armv8 -fclangir -disable-O0-optnone \
// RUN:    -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fp-armv8 -fclangir -disable-O0-optnone \
// RUN:    -fno-clangir-call-conv-lowering -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fp-armv8 -disable-O0-optnone \
// RUN:    -emit-llvm -o %t-og.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-og.ll %s

// Curated subset of the AArch32 (32-bit ARM) NEON __builtin_neon_* family
// lowered by CIRGen's emitARMBuiltinExpr. Each builtin must emit the same
// arm.neon.* (or target-independent) intrinsic as classic CodeGen (OGCG).

typedef signed char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;
typedef float f32;
typedef __attribute__((neon_vector_type(8))) i8 i8x8;
typedef __attribute__((neon_vector_type(8))) u8 u8x8;
typedef __attribute__((neon_vector_type(16))) i8 i8x16;
typedef __attribute__((neon_vector_type(4))) i16 i16x4;
typedef __attribute__((neon_vector_type(8))) i16 i16x8;
typedef __attribute__((neon_vector_type(4))) u16 u16x4;
typedef __attribute__((neon_vector_type(8))) u16 u16x8;
typedef __attribute__((neon_vector_type(2))) i32 i32x2;
typedef __attribute__((neon_vector_type(4))) i32 i32x4;
typedef __attribute__((neon_vector_type(2))) u32 u32x2;
typedef __attribute__((neon_vector_type(4))) u32 u32x4;
typedef __attribute__((neon_vector_type(2))) f32 f32x2;
typedef __attribute__((neon_vector_type(4))) f32 f32x4;

// NeonTypeFlags: s32-quad = 34, u32-quad = 50, f32-quad = 41, s8-quad = 32,
// s8 = 0, u8 = 16, s16 = 1, s16-quad = 33, s32 = 2, u16 = 17, u16-quad = 49,
// u32 = 18.

i32x4 test_vmaxq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vmaxq_v(a, b, 34);
}
// CIR-LABEL: @test_vmaxq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vmaxs"
// LLVM-LABEL: @test_vmaxq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vmaxs.v4i32
// OGCG-LABEL: @test_vmaxq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vmaxs.v4i32

u32x4 test_vmaxq_u32(u32x4 a, u32x4 b) {
  return __builtin_neon_vmaxq_v(a, b, 50);
}
// CIR-LABEL: @test_vmaxq_u32
// CIR: cir.call_llvm_intrinsic "arm.neon.vmaxu"
// LLVM-LABEL: @test_vmaxq_u32
// LLVM: call <4 x i32> @llvm.arm.neon.vmaxu.v4i32
// OGCG-LABEL: @test_vmaxq_u32
// OGCG: call <4 x i32> @llvm.arm.neon.vmaxu.v4i32

i32x4 test_vminq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vminq_v(a, b, 34);
}
// CIR-LABEL: @test_vminq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vmins"
// LLVM-LABEL: @test_vminq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vmins.v4i32
// OGCG-LABEL: @test_vminq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vmins.v4i32

i32x4 test_vabdq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vabdq_v(a, b, 34);
}
// CIR-LABEL: @test_vabdq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vabds"
// LLVM-LABEL: @test_vabdq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vabds.v4i32
// OGCG-LABEL: @test_vabdq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vabds.v4i32

i32x4 test_vhaddq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vhaddq_v(a, b, 34);
}
// CIR-LABEL: @test_vhaddq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vhadds"
// LLVM-LABEL: @test_vhaddq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vhadds.v4i32
// OGCG-LABEL: @test_vhaddq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vhadds.v4i32

i32x4 test_vrhaddq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vrhaddq_v(a, b, 34);
}
// CIR-LABEL: @test_vrhaddq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrhadds"
// LLVM-LABEL: @test_vrhaddq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vrhadds.v4i32
// OGCG-LABEL: @test_vrhaddq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vrhadds.v4i32

i32x4 test_vshlq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vshlq_v(a, b, 34);
}
// CIR-LABEL: @test_vshlq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vshifts"
// LLVM-LABEL: @test_vshlq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vshifts.v4i32
// OGCG-LABEL: @test_vshlq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vshifts.v4i32

u32x4 test_vrshlq_u32(u32x4 a, u32x4 b) {
  return __builtin_neon_vrshlq_v(a, b, 50);
}
// CIR-LABEL: @test_vrshlq_u32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrshiftu"
// LLVM-LABEL: @test_vrshlq_u32
// LLVM: call <4 x i32> @llvm.arm.neon.vrshiftu.v4i32
// OGCG-LABEL: @test_vrshlq_u32
// OGCG: call <4 x i32> @llvm.arm.neon.vrshiftu.v4i32

i32x4 test_vqshlq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vqshlq_v(a, b, 34);
}
// CIR-LABEL: @test_vqshlq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vqshifts"
// LLVM-LABEL: @test_vqshlq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vqshifts.v4i32
// OGCG-LABEL: @test_vqshlq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vqshifts.v4i32

i32x4 test_vqaddq_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vqaddq_v(a, b, 34);
}
// CIR-LABEL: @test_vqaddq_s32
// CIR: cir.call_llvm_intrinsic "sadd.sat"
// LLVM-LABEL: @test_vqaddq_s32
// LLVM: call <4 x i32> @llvm.sadd.sat.v4i32
// OGCG-LABEL: @test_vqaddq_s32
// OGCG: call <4 x i32> @llvm.sadd.sat.v4i32

u32x4 test_vqsubq_u32(u32x4 a, u32x4 b) {
  return __builtin_neon_vqsubq_v(a, b, 50);
}
// CIR-LABEL: @test_vqsubq_u32
// CIR: cir.call_llvm_intrinsic "usub.sat"
// LLVM-LABEL: @test_vqsubq_u32
// LLVM: call <4 x i32> @llvm.usub.sat.v4i32
// OGCG-LABEL: @test_vqsubq_u32
// OGCG: call <4 x i32> @llvm.usub.sat.v4i32

f32x4 test_vmaxnmq_f32(f32x4 a, f32x4 b) {
  return __builtin_neon_vmaxnmq_v(a, b, 41);
}
// CIR-LABEL: @test_vmaxnmq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vmaxnm"
// LLVM-LABEL: @test_vmaxnmq_f32
// LLVM: call <4 x float> @llvm.arm.neon.vmaxnm.v4f32
// OGCG-LABEL: @test_vmaxnmq_f32
// OGCG: call <4 x float> @llvm.arm.neon.vmaxnm.v4f32

f32x4 test_vminnmq_f32(f32x4 a, f32x4 b) {
  return __builtin_neon_vminnmq_v(a, b, 41);
}
// CIR-LABEL: @test_vminnmq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vminnm"
// LLVM-LABEL: @test_vminnmq_f32
// LLVM: call <4 x float> @llvm.arm.neon.vminnm.v4f32
// OGCG-LABEL: @test_vminnmq_f32
// OGCG: call <4 x float> @llvm.arm.neon.vminnm.v4f32

i32x4 test_vshlq_n_s32(i32x4 a) {
  return __builtin_neon_vshlq_n_v(a, 3, 34);
}
// CIR-LABEL: @test_vshlq_n_s32
// CIR: cir.shift(left
// LLVM-LABEL: @test_vshlq_n_s32
// LLVM: shl <4 x i32> {{.*}}, splat (i32 3)
// OGCG-LABEL: @test_vshlq_n_s32
// OGCG: shl <4 x i32> {{.*}}, splat (i32 3)

i32x4 test_vshrq_n_s32(i32x4 a) {
  return __builtin_neon_vshrq_n_v(a, 3, 34);
}
// CIR-LABEL: @test_vshrq_n_s32
// CIR: cir.shift(right
// LLVM-LABEL: @test_vshrq_n_s32
// LLVM: ashr <4 x i32> {{.*}}, splat (i32 3)
// OGCG-LABEL: @test_vshrq_n_s32
// OGCG: ashr <4 x i32> {{.*}}, splat (i32 3)

u32x4 test_vshrq_n_u32(u32x4 a) {
  return __builtin_neon_vshrq_n_v(a, 3, 50);
}
// CIR-LABEL: @test_vshrq_n_u32
// CIR: cir.shift(right
// LLVM-LABEL: @test_vshrq_n_u32
// LLVM: lshr <4 x i32> {{.*}}, splat (i32 3)
// OGCG-LABEL: @test_vshrq_n_u32
// OGCG: lshr <4 x i32> {{.*}}, splat (i32 3)

i32x4 test_vabsq_s32(i32x4 a) {
  return __builtin_neon_vabsq_v(a, 34);
}
// CIR-LABEL: @test_vabsq_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vabs"
// LLVM-LABEL: @test_vabsq_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vabs.v4i32
// OGCG-LABEL: @test_vabsq_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vabs.v4i32

f32x4 test_vabsq_f32(f32x4 a) {
  return __builtin_neon_vabsq_v(a, 41);
}
// CIR-LABEL: @test_vabsq_f32
// CIR: cir.fabs
// LLVM-LABEL: @test_vabsq_f32
// LLVM: call <4 x float> @llvm.fabs.v4f32
// OGCG-LABEL: @test_vabsq_f32
// OGCG: call <4 x float> @llvm.fabs.v4f32

i32x4 test_vclzq_s32(i32x4 a) {
  return __builtin_neon_vclzq_v(a, 34);
}
// CIR-LABEL: @test_vclzq_s32
// CIR: cir.call_llvm_intrinsic "ctlz"
// LLVM-LABEL: @test_vclzq_s32
// LLVM: call <4 x i32> @llvm.ctlz.v4i32
// OGCG-LABEL: @test_vclzq_s32
// OGCG: call <4 x i32> @llvm.ctlz.v4i32

i8x16 test_vcntq_s8(i8x16 a) {
  return __builtin_neon_vcntq_v(a, 32);
}
// CIR-LABEL: @test_vcntq_s8
// CIR: cir.call_llvm_intrinsic "ctpop"
// LLVM-LABEL: @test_vcntq_s8
// LLVM: call <16 x i8> @llvm.ctpop.v16i8
// OGCG-LABEL: @test_vcntq_s8
// OGCG: call <16 x i8> @llvm.ctpop.v16i8

// ---- Contiguous vector loads/stores (arm.neon.vld1/vst1 intrinsics) ----

i32x4 test_vld1q_s32(const i32 *p) {
  return __builtin_neon_vld1q_v(p, 34);
}
// CIR-LABEL: @test_vld1q_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vld1"
// LLVM-LABEL: @test_vld1q_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vld1.v4i32.p0(ptr {{.*}}, i32 4)
// OGCG-LABEL: @test_vld1q_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vld1.v4i32.p0(ptr {{.*}}, i32 4)

void test_vst1q_s32(i32 *p, i32x4 v) {
  __builtin_neon_vst1q_v(p, v, 34);
}
// CIR-LABEL: @test_vst1q_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vst1"
// LLVM-LABEL: @test_vst1q_s32
// LLVM: call void @llvm.arm.neon.vst1.p0.v4i32(ptr {{.*}}, <4 x i32> {{.*}}, i32 4)
// OGCG-LABEL: @test_vst1q_s32
// OGCG: call void @llvm.arm.neon.vst1.p0.v4i32(ptr {{.*}}, <4 x i32> {{.*}}, i32 4)

i32x4 test_vld1q_lane_s32(const i32 *p, i32x4 v) {
  return __builtin_neon_vld1q_lane_v(p, v, 2, 34);
}
// CIR-LABEL: @test_vld1q_lane_s32
// CIR: cir.load
// CIR: cir.vec.insert
// LLVM-LABEL: @test_vld1q_lane_s32
// LLVM: load i32
// LLVM: insertelement <4 x i32> {{.*}}, i32 {{.*}}, i32 2
// OGCG-LABEL: @test_vld1q_lane_s32
// OGCG: load i32
// OGCG: insertelement <4 x i32> {{.*}}, i32 {{.*}}, i32 2

i32x4 test_vld1q_dup_s32(const i32 *p) {
  return __builtin_neon_vld1q_dup_v(p, 34);
}
// CIR-LABEL: @test_vld1q_dup_s32
// CIR: cir.load
// CIR: cir.vec.splat
// LLVM-LABEL: @test_vld1q_dup_s32
// LLVM: shufflevector <4 x i32> {{.*}}, <4 x i32> zeroinitializer
// OGCG-LABEL: @test_vld1q_dup_s32
// OGCG: shufflevector <4 x i32> {{.*}}, <4 x i32> {{.*}} zeroinitializer

void test_vst1q_lane_s32(i32 *p, i32x4 v) {
  __builtin_neon_vst1q_lane_v(p, v, 2, 34);
}
// CIR-LABEL: @test_vst1q_lane_s32
// CIR: cir.vec.extract
// CIR: cir.store
// LLVM-LABEL: @test_vst1q_lane_s32
// LLVM: extractelement <4 x i32> {{.*}}, i32 2
// LLVM: store i32
// OGCG-LABEL: @test_vst1q_lane_s32
// OGCG: extractelement <4 x i32> {{.*}}, i32 2
// OGCG: store i32

// ---- Interleaved (de-interleaved) loads/stores (arm.neon.vld2/3/4 & vst2/3/4).
//      vldN returns an aggregate of N vectors stored through the return slot;
//      vstN interleaves N source vectors (already split into separate args). ----

void test_vld2_s8(void *dst, const i8 *src) {
  __builtin_neon_vld2_v(dst, src, 0);
}
// CIR-LABEL: @test_vld2_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vld2"
// LLVM-LABEL: @test_vld2_s8
// LLVM: [[R:%.*]] = call { <8 x i8>, <8 x i8> } @llvm.arm.neon.vld2.v8i8.p0(ptr {{.*}}, i32 1)
// LLVM: store { <8 x i8>, <8 x i8> } [[R]], ptr {{.*}}, align 8
// OGCG-LABEL: @test_vld2_s8
// OGCG: [[R:%.*]] = call { <8 x i8>, <8 x i8> } @llvm.arm.neon.vld2.v8i8.p0(ptr {{.*}}, i32 1)
// OGCG: store { <8 x i8>, <8 x i8> } [[R]], ptr {{.*}}, align 8

// Quad load: the 128-bit aggregate is still 8-byte aligned on AArch32.
void test_vld3q_s8(void *dst, const i8 *src) {
  __builtin_neon_vld3q_v(dst, src, 32);
}
// CIR-LABEL: @test_vld3q_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vld3"
// LLVM-LABEL: @test_vld3q_s8
// LLVM: [[R:%.*]] = call { <16 x i8>, <16 x i8>, <16 x i8> } @llvm.arm.neon.vld3.v16i8.p0(ptr {{.*}}, i32 1)
// LLVM: store { <16 x i8>, <16 x i8>, <16 x i8> } [[R]], ptr {{.*}}, align 8
// OGCG-LABEL: @test_vld3q_s8
// OGCG: [[R:%.*]] = call { <16 x i8>, <16 x i8>, <16 x i8> } @llvm.arm.neon.vld3.v16i8.p0(ptr {{.*}}, i32 1)
// OGCG: store { <16 x i8>, <16 x i8>, <16 x i8> } [[R]], ptr {{.*}}, align 8

void test_vld4q_f32(void *dst, const f32 *src) {
  __builtin_neon_vld4q_v(dst, src, 41);
}
// CIR-LABEL: @test_vld4q_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vld4"
// LLVM-LABEL: @test_vld4q_f32
// LLVM: [[R:%.*]] = call { <4 x float>, <4 x float>, <4 x float>, <4 x float> } @llvm.arm.neon.vld4.v4f32.p0(ptr {{.*}}, i32 4)
// LLVM: store { <4 x float>, <4 x float>, <4 x float>, <4 x float> } [[R]], ptr {{.*}}, align 8
// OGCG-LABEL: @test_vld4q_f32
// OGCG: [[R:%.*]] = call { <4 x float>, <4 x float>, <4 x float>, <4 x float> } @llvm.arm.neon.vld4.v4f32.p0(ptr {{.*}}, i32 4)
// OGCG: store { <4 x float>, <4 x float>, <4 x float>, <4 x float> } [[R]], ptr {{.*}}, align 8

void test_vst2_s8(i8 *dst, i8x8 a, i8x8 b) {
  __builtin_neon_vst2_v(dst, a, b, 0);
}
// CIR-LABEL: @test_vst2_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vst2"
// LLVM-LABEL: @test_vst2_s8
// LLVM: call void @llvm.arm.neon.vst2.p0.v8i8(ptr {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, i32 1)
// OGCG-LABEL: @test_vst2_s8
// OGCG: call void @llvm.arm.neon.vst2.p0.v8i8(ptr {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, i32 1)

void test_vst3_s16(i16 *dst, i16x4 a, i16x4 b, i16x4 c) {
  __builtin_neon_vst3_v(dst, a, b, c, 1);
}
// CIR-LABEL: @test_vst3_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vst3"
// LLVM-LABEL: @test_vst3_s16
// LLVM: call void @llvm.arm.neon.vst3.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, i32 2)
// OGCG-LABEL: @test_vst3_s16
// OGCG: call void @llvm.arm.neon.vst3.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, i32 2)

void test_vst4q_f32(f32 *dst, f32x4 a, f32x4 b, f32x4 c, f32x4 d) {
  __builtin_neon_vst4q_v(dst, a, b, c, d, 41);
}
// CIR-LABEL: @test_vst4q_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vst4"
// LLVM-LABEL: @test_vst4q_f32
// LLVM: call void @llvm.arm.neon.vst4.p0.v4f32(ptr {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}, i32 4)
// OGCG-LABEL: @test_vst4q_f32
// OGCG: call void @llvm.arm.neon.vst4.p0.v4f32(ptr {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}, i32 4)

// Load-and-duplicate: same aggregate return shape, arm.neon.vldNdup intrinsic.
void test_vld2_dup_s8(void *dst, const i8 *src) {
  __builtin_neon_vld2_dup_v(dst, src, 0);
}
// CIR-LABEL: @test_vld2_dup_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vld2dup"
// LLVM-LABEL: @test_vld2_dup_s8
// LLVM: call { <8 x i8>, <8 x i8> } @llvm.arm.neon.vld2dup.v8i8.p0(ptr {{.*}}, i32 1)
// OGCG-LABEL: @test_vld2_dup_s8
// OGCG: call { <8 x i8>, <8 x i8> } @llvm.arm.neon.vld2dup.v8i8.p0(ptr {{.*}}, i32 1)

void test_vld3q_dup_s32(void *dst, const i32 *src) {
  __builtin_neon_vld3q_dup_v(dst, src, 34);
}
// CIR-LABEL: @test_vld3q_dup_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vld3dup"
// LLVM-LABEL: @test_vld3q_dup_s32
// LLVM: call { <4 x i32>, <4 x i32>, <4 x i32> } @llvm.arm.neon.vld3dup.v4i32.p0(ptr {{.*}}, i32 4)
// LLVM: store { <4 x i32>, <4 x i32>, <4 x i32> } {{.*}}, ptr {{.*}}, align 8
// OGCG-LABEL: @test_vld3q_dup_s32
// OGCG: call { <4 x i32>, <4 x i32>, <4 x i32> } @llvm.arm.neon.vld3dup.v4i32.p0(ptr {{.*}}, i32 4)
// OGCG: store { <4 x i32>, <4 x i32>, <4 x i32> } {{.*}}, ptr {{.*}}, align 8

// Load a single lane: intrinsic also takes the current vectors + lane index.
void test_vld2_lane_s16(void *dst, const i16 *src, i16x4 a, i16x4 b) {
  __builtin_neon_vld2_lane_v(dst, src, a, b, 1, 1);
}
// CIR-LABEL: @test_vld2_lane_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vld2lane"
// LLVM-LABEL: @test_vld2_lane_s16
// LLVM: call { <4 x i16>, <4 x i16> } @llvm.arm.neon.vld2lane.v4i16.p0(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, i32 1, i32 2)
// OGCG-LABEL: @test_vld2_lane_s16
// OGCG: call { <4 x i16>, <4 x i16> } @llvm.arm.neon.vld2lane.v4i16.p0(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, i32 1, i32 2)

void test_vld3q_lane_s32(void *dst, const i32 *src, i32x4 a, i32x4 b, i32x4 c) {
  __builtin_neon_vld3q_lane_v(dst, src, a, b, c, 2, 34);
}
// CIR-LABEL: @test_vld3q_lane_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vld3lane"
// LLVM-LABEL: @test_vld3q_lane_s32
// LLVM: call { <4 x i32>, <4 x i32>, <4 x i32> } @llvm.arm.neon.vld3lane.v4i32.p0(ptr {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, i32 2, i32 4)
// OGCG-LABEL: @test_vld3q_lane_s32
// OGCG: call { <4 x i32>, <4 x i32>, <4 x i32> } @llvm.arm.neon.vld3lane.v4i32.p0(ptr {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, i32 2, i32 4)

void test_vst2_lane_s16(i16 *dst, i16x4 a, i16x4 b) {
  __builtin_neon_vst2_lane_v(dst, a, b, 1, 1);
}
// CIR-LABEL: @test_vst2_lane_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vst2lane"
// LLVM-LABEL: @test_vst2_lane_s16
// LLVM: call void @llvm.arm.neon.vst2lane.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, i32 1, i32 2)
// OGCG-LABEL: @test_vst2_lane_s16
// OGCG: call void @llvm.arm.neon.vst2lane.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, i32 1, i32 2)

void test_vst4q_lane_s32(i32 *dst, i32x4 a, i32x4 b, i32x4 c, i32x4 d) {
  __builtin_neon_vst4q_lane_v(dst, a, b, c, d, 2, 34);
}
// CIR-LABEL: @test_vst4q_lane_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vst4lane"
// LLVM-LABEL: @test_vst4q_lane_s32
// LLVM: call void @llvm.arm.neon.vst4lane.p0.v4i32(ptr {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, i32 2, i32 4)
// OGCG-LABEL: @test_vst4q_lane_s32
// OGCG: call void @llvm.arm.neon.vst4lane.p0.v4i32(ptr {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> {{.*}}, i32 2, i32 4)

// ---- Widening / narrowing ----

i32x4 test_vmull_s16(i16x4 a, i16x4 b) {
  return __builtin_neon_vmull_v(a, b, 34);
}
// CIR-LABEL: @test_vmull_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vmulls"
// LLVM-LABEL: @test_vmull_s16
// LLVM: call <4 x i32> @llvm.arm.neon.vmulls.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// OGCG-LABEL: @test_vmull_s16
// OGCG: call <4 x i32> @llvm.arm.neon.vmulls.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})

u32x4 test_vmull_u16(i16x4 a, i16x4 b) {
  return __builtin_neon_vmull_v(a, b, 50);
}
// CIR-LABEL: @test_vmull_u16
// CIR: cir.call_llvm_intrinsic "arm.neon.vmullu"
// LLVM-LABEL: @test_vmull_u16
// LLVM: call <4 x i32> @llvm.arm.neon.vmullu.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// OGCG-LABEL: @test_vmull_u16
// OGCG: call <4 x i32> @llvm.arm.neon.vmullu.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})

i16x8 test_vmovl_s8(i8x8 a) {
  return __builtin_neon_vmovl_v(a, 33);
}
// CIR-LABEL: @test_vmovl_s8
// CIR: cir.cast integral
// LLVM-LABEL: @test_vmovl_s8
// LLVM: sext <8 x i8> {{.*}} to <8 x i16>
// OGCG-LABEL: @test_vmovl_s8
// OGCG: sext <8 x i8> {{.*}} to <8 x i16>

i8x8 test_vmovn_s16(i16x8 a) {
  return __builtin_neon_vmovn_v(a, 0);
}
// CIR-LABEL: @test_vmovn_s16
// CIR: cir.cast integral
// LLVM-LABEL: @test_vmovn_s16
// LLVM: trunc <8 x i16> {{.*}} to <8 x i8>
// OGCG-LABEL: @test_vmovn_s16
// OGCG: trunc <8 x i16> {{.*}} to <8 x i8>

i8x8 test_vqmovn_s16(i16x8 a) {
  return __builtin_neon_vqmovn_v(a, 0);
}
// CIR-LABEL: @test_vqmovn_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqmovns"
// LLVM-LABEL: @test_vqmovn_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vqmovns.v8i8(<8 x i16> {{.*}})
// OGCG-LABEL: @test_vqmovn_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vqmovns.v8i8(<8 x i16> {{.*}})

i8x8 test_vqmovun_s16(i16x8 a) {
  return __builtin_neon_vqmovun_v(a, 16);
}
// CIR-LABEL: @test_vqmovun_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqmovnsu"
// LLVM-LABEL: @test_vqmovun_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vqmovnsu.v8i8(<8 x i16> {{.*}})
// OGCG-LABEL: @test_vqmovun_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vqmovnsu.v8i8(<8 x i16> {{.*}})

// ---- Pairwise ----

i32x2 test_vpadd_s32(i32x2 a, i32x2 b) {
  return __builtin_neon_vpadd_v(a, b, 2);
}
// CIR-LABEL: @test_vpadd_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vpadd"
// LLVM-LABEL: @test_vpadd_s32
// LLVM: call <2 x i32> @llvm.arm.neon.vpadd.v2i32(<2 x i32> {{.*}}, <2 x i32> {{.*}})
// OGCG-LABEL: @test_vpadd_s32
// OGCG: call <2 x i32> @llvm.arm.neon.vpadd.v2i32(<2 x i32> {{.*}}, <2 x i32> {{.*}})

i16x4 test_vpaddl_s8(i8x8 a) {
  return __builtin_neon_vpaddl_v(a, 1);
}
// CIR-LABEL: @test_vpaddl_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vpaddls"
// LLVM-LABEL: @test_vpaddl_s8
// LLVM: call <4 x i16> @llvm.arm.neon.vpaddls.v4i16.v8i8(<8 x i8> {{.*}})
// OGCG-LABEL: @test_vpaddl_s8
// OGCG: call <4 x i16> @llvm.arm.neon.vpaddls.v4i16.v8i8(<8 x i8> {{.*}})

i16x8 test_vpaddlq_s8(i8x16 a) {
  return __builtin_neon_vpaddlq_v(a, 33);
}
// CIR-LABEL: @test_vpaddlq_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vpaddls"
// LLVM-LABEL: @test_vpaddlq_s8
// LLVM: call <8 x i16> @llvm.arm.neon.vpaddls.v8i16.v16i8(<16 x i8> {{.*}})
// OGCG-LABEL: @test_vpaddlq_s8
// OGCG: call <8 x i16> @llvm.arm.neon.vpaddls.v8i16.v16i8(<16 x i8> {{.*}})

i16x4 test_vpadal_s8(i16x4 a, i8x8 b) {
  return __builtin_neon_vpadal_v(a, b, 1);
}
// CIR-LABEL: @test_vpadal_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vpadals"
// LLVM-LABEL: @test_vpadal_s8
// LLVM: call <4 x i16> @llvm.arm.neon.vpadals.v4i16.v8i8(<4 x i16> {{.*}}, <8 x i8> {{.*}})
// OGCG-LABEL: @test_vpadal_s8
// OGCG: call <4 x i16> @llvm.arm.neon.vpadals.v4i16.v8i8(<4 x i16> {{.*}}, <8 x i8> {{.*}})

// ---- Immediate widening / narrowing / insert / accumulate shifts ----

i16x8 test_vshll_n_s8(i8x8 a) {
  return __builtin_neon_vshll_n_v(a, 3, 33);
}
// CIR-LABEL: @test_vshll_n_s8
// CIR: cir.cast integral
// CIR: cir.shift(left
// LLVM-LABEL: @test_vshll_n_s8
// LLVM: sext <8 x i8> {{.*}} to <8 x i16>
// LLVM: shl <8 x i16> {{.*}}, splat (i16 3)
// OGCG-LABEL: @test_vshll_n_s8
// OGCG: sext <8 x i8> {{.*}} to <8 x i16>
// OGCG: shl <8 x i16> {{.*}}, splat (i16 3)

i8x8 test_vshrn_n_s16(i16x8 a) {
  return __builtin_neon_vshrn_n_v(a, 3, 0);
}
// CIR-LABEL: @test_vshrn_n_s16
// CIR: cir.shift(right
// CIR: cir.cast integral
// LLVM-LABEL: @test_vshrn_n_s16
// LLVM: ashr <8 x i16> {{.*}}, splat (i16 3)
// LLVM: trunc <8 x i16> {{.*}} to <8 x i8>
// OGCG-LABEL: @test_vshrn_n_s16
// OGCG: ashr <8 x i16> {{.*}}, splat (i16 3)
// OGCG: trunc <8 x i16> {{.*}} to <8 x i8>

i8x8 test_vqshrn_n_s16(i16x8 a) {
  return __builtin_neon_vqshrn_n_v(a, 3, 0);
}
// CIR-LABEL: @test_vqshrn_n_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqshiftns"
// LLVM-LABEL: @test_vqshrn_n_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vqshiftns.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vqshrn_n_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vqshiftns.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

u8x8 test_vqshrn_n_u16(u16x8 a) {
  return __builtin_neon_vqshrn_n_v(a, 3, 16);
}
// CIR-LABEL: @test_vqshrn_n_u16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqshiftnu"
// LLVM-LABEL: @test_vqshrn_n_u16
// LLVM: call <8 x i8> @llvm.arm.neon.vqshiftnu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vqshrn_n_u16
// OGCG: call <8 x i8> @llvm.arm.neon.vqshiftnu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

u8x8 test_vqshrun_n_s16(i16x8 a) {
  return __builtin_neon_vqshrun_n_v(a, 3, 16);
}
// CIR-LABEL: @test_vqshrun_n_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqshiftnsu"
// LLVM-LABEL: @test_vqshrun_n_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vqshiftnsu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vqshrun_n_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vqshiftnsu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

i8x8 test_vrshrn_n_s16(i16x8 a) {
  return __builtin_neon_vrshrn_n_v(a, 3, 0);
}
// CIR-LABEL: @test_vrshrn_n_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vrshiftn"
// LLVM-LABEL: @test_vrshrn_n_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vrshiftn.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vrshrn_n_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vrshiftn.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

i8x8 test_vqrshrn_n_s16(i16x8 a) {
  return __builtin_neon_vqrshrn_n_v(a, 3, 0);
}
// CIR-LABEL: @test_vqrshrn_n_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqrshiftns"
// LLVM-LABEL: @test_vqrshrn_n_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vqrshiftns.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vqrshrn_n_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vqrshiftns.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

u8x8 test_vqrshrn_n_u16(u16x8 a) {
  return __builtin_neon_vqrshrn_n_v(a, 3, 16);
}
// CIR-LABEL: @test_vqrshrn_n_u16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqrshiftnu"
// LLVM-LABEL: @test_vqrshrn_n_u16
// LLVM: call <8 x i8> @llvm.arm.neon.vqrshiftnu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vqrshrn_n_u16
// OGCG: call <8 x i8> @llvm.arm.neon.vqrshiftnu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

u8x8 test_vqrshrun_n_s16(i16x8 a) {
  return __builtin_neon_vqrshrun_n_v(a, 3, 16);
}
// CIR-LABEL: @test_vqrshrun_n_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqrshiftnsu"
// LLVM-LABEL: @test_vqrshrun_n_s16
// LLVM: call <8 x i8> @llvm.arm.neon.vqrshiftnsu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))
// OGCG-LABEL: @test_vqrshrun_n_s16
// OGCG: call <8 x i8> @llvm.arm.neon.vqrshiftnsu.v8i8(<8 x i16> {{.*}}, <8 x i16> splat (i16 -3))

i32x4 test_vsliq_n_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vsliq_n_v(a, b, 3, 34);
}
// CIR-LABEL: @test_vsliq_n_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vshiftins"
// LLVM-LABEL: @test_vsliq_n_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vshiftins.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> splat (i32 3))
// OGCG-LABEL: @test_vsliq_n_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vshiftins.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> splat (i32 3))

i32x4 test_vsriq_n_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vsriq_n_v(a, b, 3, 34);
}
// CIR-LABEL: @test_vsriq_n_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vshiftins"
// LLVM-LABEL: @test_vsriq_n_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vshiftins.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> splat (i32 -3))
// OGCG-LABEL: @test_vsriq_n_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vshiftins.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}}, <4 x i32> splat (i32 -3))

i32x4 test_vsraq_n_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vsraq_n_v(a, b, 3, 34);
}
// CIR-LABEL: @test_vsraq_n_s32
// CIR: cir.shift(right
// CIR: cir.binop(add
// LLVM-LABEL: @test_vsraq_n_s32
// LLVM: ashr <4 x i32> {{.*}}, splat (i32 3)
// LLVM: add <4 x i32>
// OGCG-LABEL: @test_vsraq_n_s32
// OGCG: ashr <4 x i32> {{.*}}, splat (i32 3)
// OGCG: add <4 x i32>

u32x4 test_vsraq_n_u32(u32x4 a, u32x4 b) {
  return __builtin_neon_vsraq_n_v(a, b, 3, 50);
}
// CIR-LABEL: @test_vsraq_n_u32
// CIR: cir.shift(right
// CIR: cir.binop(add
// LLVM-LABEL: @test_vsraq_n_u32
// LLVM: lshr <4 x i32> {{.*}}, splat (i32 3)
// LLVM: add <4 x i32>
// OGCG-LABEL: @test_vsraq_n_u32
// OGCG: lshr <4 x i32> {{.*}}, splat (i32 3)
// OGCG: add <4 x i32>

i32x4 test_vrsraq_n_s32(i32x4 a, i32x4 b) {
  return __builtin_neon_vrsraq_n_v(a, b, 3, 34);
}
// CIR-LABEL: @test_vrsraq_n_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrshifts"
// CIR: cir.binop(add
// LLVM-LABEL: @test_vrsraq_n_s32
// LLVM: call <4 x i32> @llvm.arm.neon.vrshifts.v4i32(<4 x i32> {{.*}}, <4 x i32> splat (i32 -3))
// LLVM: add <4 x i32>
// OGCG-LABEL: @test_vrsraq_n_s32
// OGCG: call <4 x i32> @llvm.arm.neon.vrshifts.v4i32(<4 x i32> {{.*}}, <4 x i32> splat (i32 -3))
// OGCG: add <4 x i32>

u32x4 test_vrsraq_n_u32(u32x4 a, u32x4 b) {
  return __builtin_neon_vrsraq_n_v(a, b, 3, 50);
}
// CIR-LABEL: @test_vrsraq_n_u32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrshiftu"
// CIR: cir.binop(add
// LLVM-LABEL: @test_vrsraq_n_u32
// LLVM: call <4 x i32> @llvm.arm.neon.vrshiftu.v4i32(<4 x i32> {{.*}}, <4 x i32> splat (i32 -3))
// LLVM: add <4 x i32>
// OGCG-LABEL: @test_vrsraq_n_u32
// OGCG: call <4 x i32> @llvm.arm.neon.vrshiftu.v4i32(<4 x i32> {{.*}}, <4 x i32> splat (i32 -3))
// OGCG: add <4 x i32>

// ---- Saturating doubling multiply ----

i32x4 test_vqdmull_s16(i16x4 a, i16x4 b) {
  return __builtin_neon_vqdmull_v(a, b, 34);
}
// CIR-LABEL: @test_vqdmull_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqdmull"
// LLVM-LABEL: @test_vqdmull_s16
// LLVM: call <4 x i32> @llvm.arm.neon.vqdmull.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// OGCG-LABEL: @test_vqdmull_s16
// OGCG: call <4 x i32> @llvm.arm.neon.vqdmull.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})

i32x4 test_vqdmlal_s16(i32x4 a, i16x4 b, i16x4 c) {
  return __builtin_neon_vqdmlal_v(a, b, c, 34);
}
// CIR-LABEL: @test_vqdmlal_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqdmull"
// CIR: cir.call_llvm_intrinsic "sadd.sat"
// LLVM-LABEL: @test_vqdmlal_s16
// LLVM: call <4 x i32> @llvm.arm.neon.vqdmull.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// LLVM: call <4 x i32> @llvm.sadd.sat.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}})
// OGCG-LABEL: @test_vqdmlal_s16
// OGCG: call <4 x i32> @llvm.arm.neon.vqdmull.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// OGCG: call <4 x i32> @llvm.sadd.sat.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}})

i32x4 test_vqdmlsl_s16(i32x4 a, i16x4 b, i16x4 c) {
  return __builtin_neon_vqdmlsl_v(a, b, c, 34);
}
// CIR-LABEL: @test_vqdmlsl_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqdmull"
// CIR: cir.call_llvm_intrinsic "ssub.sat"
// LLVM-LABEL: @test_vqdmlsl_s16
// LLVM: call <4 x i32> @llvm.arm.neon.vqdmull.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// LLVM: call <4 x i32> @llvm.ssub.sat.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}})
// OGCG-LABEL: @test_vqdmlsl_s16
// OGCG: call <4 x i32> @llvm.arm.neon.vqdmull.v4i32(<4 x i16> {{.*}}, <4 x i16> {{.*}})
// OGCG: call <4 x i32> @llvm.ssub.sat.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}})

i16x8 test_vqdmulhq_s16(i16x8 a, i16x8 b) {
  return __builtin_neon_vqdmulhq_v(a, b, 33);
}
// CIR-LABEL: @test_vqdmulhq_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqdmulh"
// LLVM-LABEL: @test_vqdmulhq_s16
// LLVM: call <8 x i16> @llvm.arm.neon.vqdmulh.v8i16(<8 x i16> {{.*}}, <8 x i16> {{.*}})
// OGCG-LABEL: @test_vqdmulhq_s16
// OGCG: call <8 x i16> @llvm.arm.neon.vqdmulh.v8i16(<8 x i16> {{.*}}, <8 x i16> {{.*}})

i16x8 test_vqrdmulhq_s16(i16x8 a, i16x8 b) {
  return __builtin_neon_vqrdmulhq_v(a, b, 33);
}
// CIR-LABEL: @test_vqrdmulhq_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vqrdmulh"
// LLVM-LABEL: @test_vqrdmulhq_s16
// LLVM: call <8 x i16> @llvm.arm.neon.vqrdmulh.v8i16(<8 x i16> {{.*}}, <8 x i16> {{.*}})
// OGCG-LABEL: @test_vqrdmulhq_s16
// OGCG: call <8 x i16> @llvm.arm.neon.vqrdmulh.v8i16(<8 x i16> {{.*}}, <8 x i16> {{.*}})

// ---- Reciprocal / reciprocal-sqrt estimate and Newton-Raphson step ----

f32x4 test_vrecpeq_f32(f32x4 a) {
  return __builtin_neon_vrecpeq_v(a, 41);
}
// CIR-LABEL: @test_vrecpeq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrecpe"
// LLVM-LABEL: @test_vrecpeq_f32
// LLVM: call <4 x float> @llvm.arm.neon.vrecpe.v4f32(<4 x float> {{.*}})
// OGCG-LABEL: @test_vrecpeq_f32
// OGCG: call <4 x float> @llvm.arm.neon.vrecpe.v4f32(<4 x float> {{.*}})

f32x4 test_vrsqrteq_f32(f32x4 a) {
  return __builtin_neon_vrsqrteq_v(a, 41);
}
// CIR-LABEL: @test_vrsqrteq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrsqrte"
// LLVM-LABEL: @test_vrsqrteq_f32
// LLVM: call <4 x float> @llvm.arm.neon.vrsqrte.v4f32(<4 x float> {{.*}})
// OGCG-LABEL: @test_vrsqrteq_f32
// OGCG: call <4 x float> @llvm.arm.neon.vrsqrte.v4f32(<4 x float> {{.*}})

u32x4 test_vrecpeq_u32(u32x4 a) {
  return __builtin_neon_vrecpeq_v(a, 50);
}
// CIR-LABEL: @test_vrecpeq_u32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrecpe"
// LLVM-LABEL: @test_vrecpeq_u32
// LLVM: call <4 x i32> @llvm.arm.neon.vrecpe.v4i32(<4 x i32> {{.*}})
// OGCG-LABEL: @test_vrecpeq_u32
// OGCG: call <4 x i32> @llvm.arm.neon.vrecpe.v4i32(<4 x i32> {{.*}})

f32x4 test_vrecpsq_f32(f32x4 a, f32x4 b) {
  return __builtin_neon_vrecpsq_v(a, b, 41);
}
// CIR-LABEL: @test_vrecpsq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrecps"
// LLVM-LABEL: @test_vrecpsq_f32
// LLVM: call <4 x float> @llvm.arm.neon.vrecps.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})
// OGCG-LABEL: @test_vrecpsq_f32
// OGCG: call <4 x float> @llvm.arm.neon.vrecps.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})

f32x4 test_vrsqrtsq_f32(f32x4 a, f32x4 b) {
  return __builtin_neon_vrsqrtsq_v(a, b, 41);
}
// CIR-LABEL: @test_vrsqrtsq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vrsqrts"
// LLVM-LABEL: @test_vrsqrtsq_f32
// LLVM: call <4 x float> @llvm.arm.neon.vrsqrts.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})
// OGCG-LABEL: @test_vrsqrtsq_f32
// OGCG: call <4 x float> @llvm.arm.neon.vrsqrts.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})

// ---- Fixed-point <-> floating-point conversion with immediate frac bits ----

f32x4 test_vcvtq_n_f32_s32(i32x4 a) {
  return __builtin_neon_vcvtq_n_f32_v(a, 3, 34);
}
// CIR-LABEL: @test_vcvtq_n_f32_s32
// CIR: cir.call_llvm_intrinsic "arm.neon.vcvtfxs2fp"
// LLVM-LABEL: @test_vcvtq_n_f32_s32
// LLVM: call <4 x float> @llvm.arm.neon.vcvtfxs2fp.v4f32.v4i32(<4 x i32> {{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtq_n_f32_s32
// OGCG: call <4 x float> @llvm.arm.neon.vcvtfxs2fp.v4f32.v4i32(<4 x i32> {{.*}}, i32 3)

f32x4 test_vcvtq_n_f32_u32(u32x4 a) {
  return __builtin_neon_vcvtq_n_f32_v(a, 3, 50);
}
// CIR-LABEL: @test_vcvtq_n_f32_u32
// CIR: cir.call_llvm_intrinsic "arm.neon.vcvtfxu2fp"
// LLVM-LABEL: @test_vcvtq_n_f32_u32
// LLVM: call <4 x float> @llvm.arm.neon.vcvtfxu2fp.v4f32.v4i32(<4 x i32> {{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtq_n_f32_u32
// OGCG: call <4 x float> @llvm.arm.neon.vcvtfxu2fp.v4f32.v4i32(<4 x i32> {{.*}}, i32 3)

i32x4 test_vcvtq_n_s32_f32(f32x4 a) {
  return __builtin_neon_vcvtq_n_s32_v(a, 3, 34);
}
// CIR-LABEL: @test_vcvtq_n_s32_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vcvtfp2fxs"
// LLVM-LABEL: @test_vcvtq_n_s32_f32
// LLVM: call <4 x i32> @llvm.arm.neon.vcvtfp2fxs.v4i32.v4f32(<4 x float> {{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtq_n_s32_f32
// OGCG: call <4 x i32> @llvm.arm.neon.vcvtfp2fxs.v4i32.v4f32(<4 x float> {{.*}}, i32 3)

u32x4 test_vcvtq_n_u32_f32(f32x4 a) {
  return __builtin_neon_vcvtq_n_u32_v(a, 3, 50);
}
// CIR-LABEL: @test_vcvtq_n_u32_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vcvtfp2fxu"
// LLVM-LABEL: @test_vcvtq_n_u32_f32
// LLVM: call <4 x i32> @llvm.arm.neon.vcvtfp2fxu.v4i32.v4f32(<4 x float> {{.*}}, i32 3)
// OGCG-LABEL: @test_vcvtq_n_u32_f32
// OGCG: call <4 x i32> @llvm.arm.neon.vcvtfp2fxu.v4i32.v4f32(<4 x float> {{.*}}, i32 3)

// Fused multiply-accumulate. The NEON builtin passes the accumulator first;
// llvm.fma wants it last, so the operands are reordered. vfms is composed by
// arm_neon.h as vfma with a negated multiply operand, so it lowers the same way.
f32x2 test_vfma_f32(f32x2 a, f32x2 b, f32x2 c) {
  return __builtin_neon_vfma_v(a, b, c, 9);
}
// CIR-LABEL: @test_vfma_f32
// CIR: cir.call_llvm_intrinsic "fma"
// LLVM-LABEL: @test_vfma_f32
// LLVM: call <2 x float> @llvm.fma.v2f32
// OGCG-LABEL: @test_vfma_f32
// OGCG: call <2 x float> @llvm.fma.v2f32

f32x4 test_vfmaq_f32(f32x4 a, f32x4 b, f32x4 c) {
  return __builtin_neon_vfmaq_v(a, b, c, 41);
}
// CIR-LABEL: @test_vfmaq_f32
// CIR: cir.call_llvm_intrinsic "fma"
// LLVM-LABEL: @test_vfmaq_f32
// LLVM: call <4 x float> @llvm.fma.v4f32
// OGCG-LABEL: @test_vfmaq_f32
// OGCG: call <4 x float> @llvm.fma.v4f32

// Bitwise select lowers to @llvm.arm.neon.vbsl on AArch32 (AArch64 expands to
// and/and-not/or instead). arm_neon.h bitcasts the operands to the byte vector
// and the builtin returns that byte vector, so the .v8i8 / .v16i8 overload is
// used regardless of the logical element type.
i8x8 test_vbsl_s8(u8x8 m, i8x8 a, i8x8 b) {
  return __builtin_neon_vbsl_v(m, a, b, 0);
}
// CIR-LABEL: @test_vbsl_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vbsl" {{.*}} -> !cir.vector<8 x !s8i>
// LLVM-LABEL: @test_vbsl_s8
// LLVM: call <8 x i8> @llvm.arm.neon.vbsl.v8i8
// OGCG-LABEL: @test_vbsl_s8
// OGCG: call <8 x i8> @llvm.arm.neon.vbsl.v8i8

i16x8 test_vbslq_s16(u16x8 m, i16x8 a, i16x8 b) {
  return __builtin_neon_vbslq_v(m, a, b, 33);
}
// CIR-LABEL: @test_vbslq_s16
// CIR: cir.call_llvm_intrinsic "arm.neon.vbsl" {{.*}} -> !cir.vector<16 x !s8i>
// LLVM-LABEL: @test_vbslq_s16
// LLVM: call <16 x i8> @llvm.arm.neon.vbsl.v16i8
// OGCG-LABEL: @test_vbslq_s16
// OGCG: call <16 x i8> @llvm.arm.neon.vbsl.v16i8

// Extract/concatenate: a shufflevector whose mask starts at the immediate lane.
i8x8 test_vext_s8(i8x8 a, i8x8 b) {
  return __builtin_neon_vext_v(a, b, 3, 0);
}
// CIR-LABEL: @test_vext_s8
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<3> : !s32i, #cir.int<4> : !s32i, #cir.int<5> : !s32i, #cir.int<6> : !s32i, #cir.int<7> : !s32i, #cir.int<8> : !s32i, #cir.int<9> : !s32i, #cir.int<10> : !s32i]
// LLVM-LABEL: @test_vext_s8
// LLVM: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10>
// OGCG-LABEL: @test_vext_s8
// OGCG: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10>

i16x8 test_vextq_s16(i16x8 a, i16x8 b) {
  return __builtin_neon_vextq_v(a, b, 2, 33);
}
// CIR-LABEL: @test_vextq_s16
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<2> : !s32i, #cir.int<3> : !s32i, #cir.int<4> : !s32i, #cir.int<5> : !s32i, #cir.int<6> : !s32i, #cir.int<7> : !s32i, #cir.int<8> : !s32i, #cir.int<9> : !s32i]
// LLVM-LABEL: @test_vextq_s16
// LLVM: shufflevector <8 x i16> {{.*}}, <8 x i16> {{.*}}, <8 x i32> <i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9>
// OGCG-LABEL: @test_vextq_s16
// OGCG: shufflevector <8 x i16> {{.*}}, <8 x i16> {{.*}}, <8 x i32> <i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9>

// Transpose/unzip/zip write a two-vector aggregate through the return-slot
// pointer as two shufflevectors. On AArch32 the stored 128-bit vector is only
// 8-byte aligned (v128:64), so the shuffle stores use align 8.
void test_vtrn_s8(void *d, i8x8 a, i8x8 b) {
  __builtin_neon_vtrn_v(d, a, b, 0);
}
// CIR-LABEL: @test_vtrn_s8
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<0> : !s32i, #cir.int<8> : !s32i, #cir.int<2> : !s32i, #cir.int<10> : !s32i, #cir.int<4> : !s32i, #cir.int<12> : !s32i, #cir.int<6> : !s32i, #cir.int<14> : !s32i]
// CIR: cir.store align(8)
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<1> : !s32i, #cir.int<9> : !s32i, #cir.int<3> : !s32i, #cir.int<11> : !s32i, #cir.int<5> : !s32i, #cir.int<13> : !s32i, #cir.int<7> : !s32i, #cir.int<15> : !s32i]
// CIR: cir.store align(8)
// LLVM-LABEL: @test_vtrn_s8
// LLVM: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 0, i32 8, i32 2, i32 10, i32 4, i32 12, i32 6, i32 14>
// LLVM: store <8 x i8> {{.*}}, align 8
// LLVM: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 1, i32 9, i32 3, i32 11, i32 5, i32 13, i32 7, i32 15>
// LLVM: store <8 x i8> {{.*}}, align 8
// OGCG-LABEL: @test_vtrn_s8
// OGCG: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 0, i32 8, i32 2, i32 10, i32 4, i32 12, i32 6, i32 14>
// OGCG: store <8 x i8> {{.*}}, align 8
// OGCG: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 1, i32 9, i32 3, i32 11, i32 5, i32 13, i32 7, i32 15>
// OGCG: store <8 x i8> {{.*}}, align 8

void test_vuzp_s8(void *d, i8x8 a, i8x8 b) {
  __builtin_neon_vuzp_v(d, a, b, 0);
}
// CIR-LABEL: @test_vuzp_s8
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<0> : !s32i, #cir.int<2> : !s32i, #cir.int<4> : !s32i, #cir.int<6> : !s32i, #cir.int<8> : !s32i, #cir.int<10> : !s32i, #cir.int<12> : !s32i, #cir.int<14> : !s32i]
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<1> : !s32i, #cir.int<3> : !s32i, #cir.int<5> : !s32i, #cir.int<7> : !s32i, #cir.int<9> : !s32i, #cir.int<11> : !s32i, #cir.int<13> : !s32i, #cir.int<15> : !s32i]
// LLVM-LABEL: @test_vuzp_s8
// LLVM: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 0, i32 2, i32 4, i32 6, i32 8, i32 10, i32 12, i32 14>
// LLVM: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 1, i32 3, i32 5, i32 7, i32 9, i32 11, i32 13, i32 15>
// OGCG-LABEL: @test_vuzp_s8
// OGCG: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 0, i32 2, i32 4, i32 6, i32 8, i32 10, i32 12, i32 14>
// OGCG: shufflevector <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i32> <i32 1, i32 3, i32 5, i32 7, i32 9, i32 11, i32 13, i32 15>

void test_vzipq_s8(void *d, i8x16 a, i8x16 b) {
  __builtin_neon_vzipq_v(d, a, b, 32);
}
// CIR-LABEL: @test_vzipq_s8
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<0> : !s32i, #cir.int<16> : !s32i, #cir.int<1> : !s32i, #cir.int<17> : !s32i, #cir.int<2> : !s32i, #cir.int<18> : !s32i, #cir.int<3> : !s32i, #cir.int<19> : !s32i, #cir.int<4> : !s32i, #cir.int<20> : !s32i, #cir.int<5> : !s32i, #cir.int<21> : !s32i, #cir.int<6> : !s32i, #cir.int<22> : !s32i, #cir.int<7> : !s32i, #cir.int<23> : !s32i]
// CIR: cir.store align(8)
// LLVM-LABEL: @test_vzipq_s8
// LLVM: shufflevector <16 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i32> <i32 0, i32 16, i32 1, i32 17, i32 2, i32 18, i32 3, i32 19, i32 4, i32 20, i32 5, i32 21, i32 6, i32 22, i32 7, i32 23>
// LLVM: store <16 x i8> {{.*}}, align 8
// OGCG-LABEL: @test_vzipq_s8
// OGCG: shufflevector <16 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i32> <i32 0, i32 16, i32 1, i32 17, i32 2, i32 18, i32 3, i32 19, i32 4, i32 20, i32 5, i32 21, i32 6, i32 22, i32 7, i32 23>
// OGCG: store <16 x i8> {{.*}}, align 8

// Broadcast one lane across every result lane: a single-source shufflevector
// with an all-lane-index mask. This is the genuine builtin the header expands
// the lane-indexed multiply family (vmul_lane / vmla_lane / vmull_lane /
// vqdmulh_lane etc.) into, so it underpins that whole family. The q / laneq
// forms grow / shrink the result width relative to the source.
i16x4 test_vsplat_lane_s16(i8x8 v) {
  return __builtin_neon_splat_lane_v(v, 1, 1);
}
// CIR-LABEL: @test_vsplat_lane_s16
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i] : !cir.vector<4 x !s16i>
// LLVM-LABEL: @test_vsplat_lane_s16
// LLVM: shufflevector <4 x i16> {{.*}}, <4 x i16> poison, <4 x i32> <i32 1, i32 1, i32 1, i32 1>
// OGCG-LABEL: @test_vsplat_lane_s16
// OGCG: shufflevector <4 x i16> {{.*}}, <4 x i16> {{.*}}, <4 x i32> <i32 1, i32 1, i32 1, i32 1>

i16x8 test_vsplatq_lane_s16(i8x8 v) {
  return __builtin_neon_splatq_lane_v(v, 1, 1);
}
// CIR-LABEL: @test_vsplatq_lane_s16
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i] : !cir.vector<8 x !s16i>
// LLVM-LABEL: @test_vsplatq_lane_s16
// LLVM: shufflevector <4 x i16> {{.*}}, <4 x i16> poison, <8 x i32> <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
// OGCG-LABEL: @test_vsplatq_lane_s16
// OGCG: shufflevector <4 x i16> {{.*}}, <4 x i16> {{.*}}, <8 x i32> <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>

i16x4 test_vsplat_laneq_s16(i8x16 v) {
  return __builtin_neon_splat_laneq_v(v, 3, 33);
}
// CIR-LABEL: @test_vsplat_laneq_s16
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<3> : !s32i, #cir.int<3> : !s32i, #cir.int<3> : !s32i, #cir.int<3> : !s32i] : !cir.vector<4 x !s16i>
// LLVM-LABEL: @test_vsplat_laneq_s16
// LLVM: shufflevector <8 x i16> {{.*}}, <8 x i16> poison, <4 x i32> <i32 3, i32 3, i32 3, i32 3>
// OGCG-LABEL: @test_vsplat_laneq_s16
// OGCG: shufflevector <8 x i16> {{.*}}, <8 x i16> {{.*}}, <4 x i32> <i32 3, i32 3, i32 3, i32 3>

i16x8 test_vsplatq_laneq_s16(i8x16 v) {
  return __builtin_neon_splatq_laneq_v(v, 2, 33);
}
// CIR-LABEL: @test_vsplatq_laneq_s16
// CIR: cir.vec.shuffle({{.*}}) [#cir.int<2> : !s32i, #cir.int<2> : !s32i, #cir.int<2> : !s32i, #cir.int<2> : !s32i, #cir.int<2> : !s32i, #cir.int<2> : !s32i, #cir.int<2> : !s32i, #cir.int<2> : !s32i] : !cir.vector<8 x !s16i>
// LLVM-LABEL: @test_vsplatq_laneq_s16
// LLVM: shufflevector <8 x i16> {{.*}}, <8 x i16> poison, <8 x i32> <i32 2, i32 2, i32 2, i32 2, i32 2, i32 2, i32 2, i32 2>
// OGCG-LABEL: @test_vsplatq_laneq_s16
// OGCG: shufflevector <8 x i16> {{.*}}, <8 x i16> {{.*}}, <8 x i32> <i32 2, i32 2, i32 2, i32 2, i32 2, i32 2, i32 2, i32 2>

// Bitwise test: (a & b) != 0 as a per-lane mask. The VecCmpOp result is
// sign-extended to the element width during lowering, matching classic's
// and / icmp-ne / sext sequence. (s16 uses NeonTypeFlags 17 / 49.)
u16x4 test_vtst_s16(i16x4 a, i16x4 b) {
  return __builtin_neon_vtst_v(a, b, 17);
}
// CIR-LABEL: @test_vtst_s16
// CIR: cir.binop(and, {{.*}}) : !cir.vector<4 x !u16i>
// CIR: cir.vec.cmp(ne, {{.*}}) : !cir.vector<4 x !u16i>, !cir.vector<4 x !u16i>
// LLVM-LABEL: @test_vtst_s16
// LLVM: and <4 x i16>
// LLVM: icmp ne <4 x i16> {{.*}}, zeroinitializer
// LLVM: sext <4 x i1> {{.*}} to <4 x i16>
// OGCG-LABEL: @test_vtst_s16
// OGCG: and <4 x i16>
// OGCG: icmp ne <4 x i16> {{.*}}, zeroinitializer
// OGCG: sext <4 x i1> {{.*}} to <4 x i16>

u16x8 test_vtstq_s16(i16x8 a, i16x8 b) {
  return __builtin_neon_vtstq_v(a, b, 49);
}
// CIR-LABEL: @test_vtstq_s16
// CIR: cir.binop(and, {{.*}}) : !cir.vector<8 x !u16i>
// CIR: cir.vec.cmp(ne, {{.*}}) : !cir.vector<8 x !u16i>, !cir.vector<8 x !u16i>
// LLVM-LABEL: @test_vtstq_s16
// LLVM: and <8 x i16>
// LLVM: icmp ne <8 x i16> {{.*}}, zeroinitializer
// LLVM: sext <8 x i1> {{.*}} to <8 x i16>
// OGCG-LABEL: @test_vtstq_s16
// OGCG: and <8 x i16>
// OGCG: icmp ne <8 x i16> {{.*}}, zeroinitializer
// OGCG: sext <8 x i1> {{.*}} to <8 x i16>

// Floating-point absolute compare: the @llvm.arm.neon.vacge / vacgt intrinsics
// take the float vectors and return the integer mask. vcale / vcalt are the
// ge / gt forms with the operands swapped. (u32 = 18, u32-quad = 50.)
u32x2 test_vcage_f32(i8x8 a, i8x8 b) {
  return __builtin_neon_vcage_v(a, b, 18);
}
// CIR-LABEL: @test_vcage_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vacge" {{.*}} : (!cir.vector<2 x !cir.float>, !cir.vector<2 x !cir.float>) -> !cir.vector<2 x !u32i>
// LLVM-LABEL: @test_vcage_f32
// LLVM: call <2 x i32> @llvm.arm.neon.vacge.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})
// OGCG-LABEL: @test_vcage_f32
// OGCG: call <2 x i32> @llvm.arm.neon.vacge.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})

u32x4 test_vcageq_f32(i8x16 a, i8x16 b) {
  return __builtin_neon_vcageq_v(a, b, 50);
}
// CIR-LABEL: @test_vcageq_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vacge" {{.*}} : (!cir.vector<4 x !cir.float>, !cir.vector<4 x !cir.float>) -> !cir.vector<4 x !u32i>
// LLVM-LABEL: @test_vcageq_f32
// LLVM: call <4 x i32> @llvm.arm.neon.vacge.v4i32.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})
// OGCG-LABEL: @test_vcageq_f32
// OGCG: call <4 x i32> @llvm.arm.neon.vacge.v4i32.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})

u32x2 test_vcagt_f32(i8x8 a, i8x8 b) {
  return __builtin_neon_vcagt_v(a, b, 18);
}
// CIR-LABEL: @test_vcagt_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vacgt" {{.*}} : (!cir.vector<2 x !cir.float>, !cir.vector<2 x !cir.float>) -> !cir.vector<2 x !u32i>
// LLVM-LABEL: @test_vcagt_f32
// LLVM: call <2 x i32> @llvm.arm.neon.vacgt.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})
// OGCG-LABEL: @test_vcagt_f32
// OGCG: call <2 x i32> @llvm.arm.neon.vacgt.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})

u32x2 test_vcale_f32(i8x8 a, i8x8 b) {
  return __builtin_neon_vcale_v(a, b, 18);
}
// CIR-LABEL: @test_vcale_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vacge" {{.*}} : (!cir.vector<2 x !cir.float>, !cir.vector<2 x !cir.float>) -> !cir.vector<2 x !u32i>
// LLVM-LABEL: @test_vcale_f32
// LLVM: call <2 x i32> @llvm.arm.neon.vacge.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})
// OGCG-LABEL: @test_vcale_f32
// OGCG: call <2 x i32> @llvm.arm.neon.vacge.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})

u32x2 test_vcalt_f32(i8x8 a, i8x8 b) {
  return __builtin_neon_vcalt_v(a, b, 18);
}
// CIR-LABEL: @test_vcalt_f32
// CIR: cir.call_llvm_intrinsic "arm.neon.vacgt" {{.*}} : (!cir.vector<2 x !cir.float>, !cir.vector<2 x !cir.float>) -> !cir.vector<2 x !u32i>
// LLVM-LABEL: @test_vcalt_f32
// LLVM: call <2 x i32> @llvm.arm.neon.vacgt.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})
// OGCG-LABEL: @test_vcalt_f32
// OGCG: call <2 x i32> @llvm.arm.neon.vacgt.v2i32.v2f32(<2 x float> {{.*}}, <2 x float> {{.*}})

// ---- Table lookup / extended table lookup (arm.neon.vtbl1-4 & vtbx1-4). On
//      AArch32 the table is passed as SEPARATE d-register args (not one wide
//      vector like AArch64); every operand and the result is <8 x i8> and the
//      intrinsics are non-overloaded, so no type suffix is mangled. ----

i8x8 test_vtbl1_s8(i8x8 t, i8x8 idx) {
  return __builtin_neon_vtbl1_v(t, idx, 0);
}
// CIR-LABEL: @test_vtbl1_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vtbl1" {{.*}} : (!cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>) -> !cir.vector<8 x !s8i>
// LLVM-LABEL: @test_vtbl1_s8
// LLVM: call <8 x i8> @llvm.arm.neon.vtbl1(<8 x i8> {{.*}}, <8 x i8> {{.*}})
// OGCG-LABEL: @test_vtbl1_s8
// OGCG: call <8 x i8> @llvm.arm.neon.vtbl1(<8 x i8> {{.*}}, <8 x i8> {{.*}})

i8x8 test_vtbl2_s8(i8x8 a, i8x8 b, i8x8 idx) {
  return __builtin_neon_vtbl2_v(a, b, idx, 0);
}
// CIR-LABEL: @test_vtbl2_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vtbl2" {{.*}} : (!cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>) -> !cir.vector<8 x !s8i>
// LLVM-LABEL: @test_vtbl2_s8
// LLVM: call <8 x i8> @llvm.arm.neon.vtbl2(<8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})
// OGCG-LABEL: @test_vtbl2_s8
// OGCG: call <8 x i8> @llvm.arm.neon.vtbl2(<8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})

// vtbx4 prepends the passthrough accumulator to the four table vectors.
i8x8 test_vtbx4_s8(i8x8 acc, i8x8 a, i8x8 b, i8x8 c, i8x8 d, i8x8 idx) {
  return __builtin_neon_vtbx4_v(acc, a, b, c, d, idx, 0);
}
// CIR-LABEL: @test_vtbx4_s8
// CIR: cir.call_llvm_intrinsic "arm.neon.vtbx4" {{.*}} : (!cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>, !cir.vector<8 x !s8i>) -> !cir.vector<8 x !s8i>
// LLVM-LABEL: @test_vtbx4_s8
// LLVM: call <8 x i8> @llvm.arm.neon.vtbx4(<8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})
// OGCG-LABEL: @test_vtbx4_s8
// OGCG: call <8 x i8> @llvm.arm.neon.vtbx4(<8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})

// ---- Non-interleaved multi-vector load/store (arm.neon.vld1xN & vst1xN).
//      vld1xN takes only the source pointer (no alignment operand, unlike
//      vldN) and returns an aggregate stored through the return slot; vst1xN
//      takes the destination pointer followed by the N source vectors. ----

void test_vld1_s8_x2(void *dst, const i8 *src) {
  __builtin_neon_vld1_x2_v(dst, src, 0);
}
// CIR-LABEL: @test_vld1_s8_x2
// CIR: cir.call_llvm_intrinsic "arm.neon.vld1x2"
// LLVM-LABEL: @test_vld1_s8_x2
// LLVM: [[R:%.*]] = call { <8 x i8>, <8 x i8> } @llvm.arm.neon.vld1x2.v8i8.p0(ptr {{.*}})
// LLVM: store { <8 x i8>, <8 x i8> } [[R]], ptr {{.*}}, align 8
// OGCG-LABEL: @test_vld1_s8_x2
// OGCG: [[R:%.*]] = call { <8 x i8>, <8 x i8> } @llvm.arm.neon.vld1x2.v8i8.p0(ptr {{.*}})
// OGCG: store { <8 x i8>, <8 x i8> } [[R]], ptr {{.*}}, align 8

void test_vld1q_f32_x4(void *dst, const f32 *src) {
  __builtin_neon_vld1q_x4_v(dst, src, 41);
}
// CIR-LABEL: @test_vld1q_f32_x4
// CIR: cir.call_llvm_intrinsic "arm.neon.vld1x4"
// LLVM-LABEL: @test_vld1q_f32_x4
// LLVM: [[R:%.*]] = call { <4 x float>, <4 x float>, <4 x float>, <4 x float> } @llvm.arm.neon.vld1x4.v4f32.p0(ptr {{.*}})
// LLVM: store { <4 x float>, <4 x float>, <4 x float>, <4 x float> } [[R]], ptr {{.*}}, align 8
// OGCG-LABEL: @test_vld1q_f32_x4
// OGCG: [[R:%.*]] = call { <4 x float>, <4 x float>, <4 x float>, <4 x float> } @llvm.arm.neon.vld1x4.v4f32.p0(ptr {{.*}})
// OGCG: store { <4 x float>, <4 x float>, <4 x float>, <4 x float> } [[R]], ptr {{.*}}, align 8

void test_vst1_s8_x2(i8 *dst, i8x8 a, i8x8 b) {
  __builtin_neon_vst1_x2_v(dst, a, b, 0);
}
// CIR-LABEL: @test_vst1_s8_x2
// CIR: cir.call_llvm_intrinsic "arm.neon.vst1x2"
// LLVM-LABEL: @test_vst1_s8_x2
// LLVM: call void @llvm.arm.neon.vst1x2.p0.v8i8(ptr {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})
// OGCG-LABEL: @test_vst1_s8_x2
// OGCG: call void @llvm.arm.neon.vst1x2.p0.v8i8(ptr {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})

void test_vst1_u16_x3(u16 *dst, u16x4 a, u16x4 b, u16x4 c) {
  __builtin_neon_vst1_x3_v(dst, a, b, c, 17);
}
// CIR-LABEL: @test_vst1_u16_x3
// CIR: cir.call_llvm_intrinsic "arm.neon.vst1x3"
// LLVM-LABEL: @test_vst1_u16_x3
// LLVM: call void @llvm.arm.neon.vst1x3.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}})
// OGCG-LABEL: @test_vst1_u16_x3
// OGCG: call void @llvm.arm.neon.vst1x3.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}}, <4 x i16> {{.*}})
