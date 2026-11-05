// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -target-feature +v8a -target-feature +fp-armv8 -fclangir \
// RUN:    -disable-O0-optnone -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -target-feature +v8a -target-feature +fp-armv8 -fclangir \
// RUN:    -disable-O0-optnone -fno-clangir-call-conv-lowering \
// RUN:    -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -target-feature +v8a -target-feature +fp-armv8 \
// RUN:    -disable-O0-optnone -emit-llvm -o %t-og.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-og.ll %s

// Test NEON builtins: vminnm, vmaxnm, vfma, vrecps, vaddv

typedef float float32_t;
typedef double float64_t;
typedef __attribute__((neon_vector_type(2))) float32_t float32x2_t;
typedef __attribute__((neon_vector_type(4))) float32_t float32x4_t;
typedef __attribute__((neon_vector_type(8))) signed char int8x8_t;
typedef __attribute__((neon_vector_type(8))) unsigned char uint8x8_t;
typedef __attribute__((neon_vector_type(16))) signed char int8x16_t;
typedef __attribute__((neon_vector_type(16))) unsigned char uint8x16_t;
typedef __attribute__((neon_vector_type(8))) signed short int16x8_t;
typedef __attribute__((neon_vector_type(8))) unsigned short uint16x8_t;
typedef signed int int32_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef long int int64_t;
typedef unsigned long int uint64_t;
typedef __attribute__((neon_vector_type(2))) int32_t int32x2_t;
typedef __attribute__((neon_vector_type(2))) uint32_t uint32x2_t;
typedef __attribute__((neon_vector_type(2))) float64_t float64x2_t;

// vminnmq_f32
float32x4_t test_vminnmq_f32(float32x4_t a, float32x4_t b) {
  return __builtin_neon_vminnmq_v(a, b, 41);
}
// CIR-LABEL: @test_vminnmq_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fminnm"
// LLVM-LABEL: @test_vminnmq_f32
// LLVM: call <4 x float> @llvm.aarch64.neon.fminnm.v4f32
// OGCG-LABEL: @test_vminnmq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fminnm.v4f32

// vminnm_f32
float32x2_t test_vminnm_f32(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vminnm_v(a, b, 9);
}
// CIR-LABEL: @test_vminnm_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fminnm"
// LLVM-LABEL: @test_vminnm_f32
// LLVM: call <2 x float> @llvm.aarch64.neon.fminnm.v2f32
// OGCG-LABEL: @test_vminnm_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fminnm.v2f32

// vmaxnmq_f32
float32x4_t test_vmaxnmq_f32(float32x4_t a, float32x4_t b) {
  return __builtin_neon_vmaxnmq_v(a, b, 41);
}
// CIR-LABEL: @test_vmaxnmq_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmaxnm"
// LLVM-LABEL: @test_vmaxnmq_f32
// LLVM: call <4 x float> @llvm.aarch64.neon.fmaxnm.v4f32
// OGCG-LABEL: @test_vmaxnmq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fmaxnm.v4f32

// vmaxnm_f32
float32x2_t test_vmaxnm_f32(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vmaxnm_v(a, b, 9);
}
// CIR-LABEL: @test_vmaxnm_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmaxnm"
// LLVM-LABEL: @test_vmaxnm_f32
// LLVM: call <2 x float> @llvm.aarch64.neon.fmaxnm.v2f32
// OGCG-LABEL: @test_vmaxnm_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fmaxnm.v2f32

// vpmaxnm_f32
float32x2_t test_vpmaxnm_f32(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vpmaxnm_v(a, b, 9);
}
// CIR-LABEL: @test_vpmaxnm_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmaxnmp"
// LLVM-LABEL: @test_vpmaxnm_f32
// LLVM: call <2 x float> @llvm.aarch64.neon.fmaxnmp.v2f32
// OGCG-LABEL: @test_vpmaxnm_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fmaxnmp.v2f32

// vpminnm_f32
float32x2_t test_vpminnm_f32(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vpminnm_v(a, b, 9);
}
// CIR-LABEL: @test_vpminnm_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fminnmp"
// LLVM-LABEL: @test_vpminnm_f32
// LLVM: call <2 x float> @llvm.aarch64.neon.fminnmp.v2f32
// OGCG-LABEL: @test_vpminnm_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fminnmp.v2f32

// vfma_f32 (non-Q, 64-bit)
float32x2_t test_vfma_f32(float32x2_t a, float32x2_t b, float32x2_t c) {
  return __builtin_neon_vfma_v(a, b, c, 9);
}
// CIR-LABEL: @test_vfma_f32
// CIR: cir.call_llvm_intrinsic "fma"
// LLVM-LABEL: @test_vfma_f32
// LLVM: call <2 x float> @llvm.fma.v2f32
// OGCG-LABEL: @test_vfma_f32
// OGCG: call <2 x float> @llvm.fma.v2f32

// vfmaq_f32 (Q, 128-bit)
float32x4_t test_vfmaq_f32(float32x4_t a, float32x4_t b, float32x4_t c) {
  return __builtin_neon_vfmaq_v(a, b, c, 41);
}
// CIR-LABEL: @test_vfmaq_f32
// CIR: cir.call_llvm_intrinsic "fma"
// LLVM-LABEL: @test_vfmaq_f32
// LLVM: call <4 x float> @llvm.fma.v4f32
// OGCG-LABEL: @test_vfmaq_f32
// OGCG: call <4 x float> @llvm.fma.v4f32

// vrecpss_f32
float32_t test_vrecpss_f32(float32_t a, float32_t b) {
  return __builtin_neon_vrecpss_f32(a, b);
}
// CIR-LABEL: @test_vrecpss_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frecps"
// LLVM-LABEL: @test_vrecpss_f32
// LLVM: call float @llvm.aarch64.neon.frecps.f32
// OGCG-LABEL: @test_vrecpss_f32
// OGCG: call float @llvm.aarch64.neon.frecps.f32

// vrecpsd_f64
float64_t test_vrecpsd_f64(float64_t a, float64_t b) {
  return __builtin_neon_vrecpsd_f64(a, b);
}
// CIR-LABEL: @test_vrecpsd_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frecps"
// LLVM-LABEL: @test_vrecpsd_f64
// LLVM: call double @llvm.aarch64.neon.frecps.f64
// OGCG-LABEL: @test_vrecpsd_f64
// OGCG: call double @llvm.aarch64.neon.frecps.f64

// vmaxnmh_f16
__attribute__((target("fullfp16")))
__fp16 test_vmaxnmh_f16(__fp16 a, __fp16 b) {
  return __builtin_neon_vmaxnmh_f16(a, b);
}
// CIR-LABEL: @test_vmaxnmh_f16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmaxnm"
// LLVM-LABEL: @test_vmaxnmh_f16
// LLVM: call half @llvm.aarch64.neon.fmaxnm.f16(half
// OGCG-LABEL: @test_vmaxnmh_f16
// OGCG: call half @llvm.aarch64.neon.fmaxnm.f16(half

// vminnmh_f16
__attribute__((target("fullfp16")))
__fp16 test_vminnmh_f16(__fp16 a, __fp16 b) {
  return __builtin_neon_vminnmh_f16(a, b);
}
// CIR-LABEL: @test_vminnmh_f16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fminnm"
// LLVM-LABEL: @test_vminnmh_f16
// LLVM: call half @llvm.aarch64.neon.fminnm.f16(half
// OGCG-LABEL: @test_vminnmh_f16
// OGCG: call half @llvm.aarch64.neon.fminnm.f16(half

// vrecpsh_f16
__attribute__((target("fullfp16")))
__fp16 test_vrecpsh_f16(__fp16 a, __fp16 b) {
  return __builtin_neon_vrecpsh_f16(a, b);
}
// CIR-LABEL: @test_vrecpsh_f16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frecps"
// LLVM-LABEL: @test_vrecpsh_f16
// LLVM: call half @llvm.aarch64.neon.frecps.f16(half
// OGCG-LABEL: @test_vrecpsh_f16
// OGCG: call half @llvm.aarch64.neon.frecps.f16(half

// vaddvq_u16
unsigned short test_vaddvq_u16(uint16x8_t a) {
  return __builtin_neon_vaddvq_u16(a);
}
// CIR-LABEL: @test_vaddvq_u16
// CIR: cir.call_llvm_intrinsic "vector.reduce.add" {{%.*}} : (!cir.vector<8 x !u16i>) -> !u16i
// LLVM-LABEL: @test_vaddvq_u16
// LLVM: call i16 @llvm.vector.reduce.add.v8i16
// OGCG-LABEL: @test_vaddvq_u16
// OGCG: call i16 @llvm.vector.reduce.add.v8i16

// vaddv_u8
unsigned char test_vaddv_u8(uint8x8_t a) {
  return __builtin_neon_vaddv_u8(a);
}
// CIR-LABEL: @test_vaddv_u8
// CIR: cir.call_llvm_intrinsic "vector.reduce.add" {{%.*}} : (!cir.vector<8 x !u8i>) -> !u8i
// LLVM-LABEL: @test_vaddv_u8
// LLVM: call i8 @llvm.vector.reduce.add.v8i8
// OGCG-LABEL: @test_vaddv_u8
// OGCG: call i8 @llvm.vector.reduce.add.v8i8

// vaddv_s8
signed char test_vaddv_s8(int8x8_t a) {
  return __builtin_neon_vaddv_s8(a);
}
// CIR-LABEL: @test_vaddv_s8
// CIR: cir.call_llvm_intrinsic "vector.reduce.add" {{%.*}} : (!cir.vector<8 x !s8i>) -> !s8i
// LLVM-LABEL: @test_vaddv_s8
// LLVM: call i8 @llvm.vector.reduce.add.v8i8
// OGCG-LABEL: @test_vaddv_s8
// OGCG: call i8 @llvm.vector.reduce.add.v8i8

// vaddvq_u8
unsigned char test_vaddvq_u8(uint8x16_t a) {
  return __builtin_neon_vaddvq_u8(a);
}
// CIR-LABEL: @test_vaddvq_u8
// CIR: cir.call_llvm_intrinsic "vector.reduce.add" {{%.*}} : (!cir.vector<16 x !u8i>) -> !u8i
// LLVM-LABEL: @test_vaddvq_u8
// LLVM: call i8 @llvm.vector.reduce.add.v16i8
// OGCG-LABEL: @test_vaddvq_u8
// OGCG: call i8 @llvm.vector.reduce.add.v16i8

// vaddvq_s8
signed char test_vaddvq_s8(int8x16_t a) {
  return __builtin_neon_vaddvq_s8(a);
}
// CIR-LABEL: @test_vaddvq_s8
// CIR: cir.call_llvm_intrinsic "vector.reduce.add" {{%.*}} : (!cir.vector<16 x !s8i>) -> !s8i
// LLVM-LABEL: @test_vaddvq_s8
// LLVM: call i8 @llvm.vector.reduce.add.v16i8
// OGCG-LABEL: @test_vaddvq_s8
// OGCG: call i8 @llvm.vector.reduce.add.v16i8

// vabds_f32
float32_t test_vabds_f32(float32_t a, float32_t b) {
  return __builtin_neon_vabds_f32(a, b);
}
// CIR-LABEL: @test_vabds_f32
// CIR: cir.call_llvm_intrinsic "aarch64.sisd.fabd"
// LLVM-LABEL: @test_vabds_f32
// LLVM: call float @llvm.aarch64.sisd.fabd.f32
// OGCG-LABEL: @test_vabds_f32
// OGCG: call float @llvm.aarch64.sisd.fabd.f32

// vaddlv_s32
int64_t test_vaddlv_s32(int32x2_t a) {
  return __builtin_neon_vaddlv_s32(a);
}
// CIR-LABEL: @test_vaddlv_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.saddlv"
// LLVM-LABEL: @test_vaddlv_s32
// LLVM: call i64 @llvm.aarch64.neon.saddlv.i64.v2i32
// OGCG-LABEL: @test_vaddlv_s32
// OGCG: call i64 @llvm.aarch64.neon.saddlv.i64.v2i32

// vaddlv_u32
uint64_t test_vaddlv_u32(uint32x2_t a) {
  return __builtin_neon_vaddlv_u32(a);
}
// CIR-LABEL: @test_vaddlv_u32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uaddlv"
// LLVM-LABEL: @test_vaddlv_u32
// LLVM: call i64 @llvm.aarch64.neon.uaddlv.i64.v2i32
// OGCG-LABEL: @test_vaddlv_u32
// OGCG: call i64 @llvm.aarch64.neon.uaddlv.i64.v2i32

// vaddlv_u8
uint16_t test_vaddlv_u8(uint8x8_t a) {
  return __builtin_neon_vaddlv_u8(a);
}
// CIR-LABEL: @test_vaddlv_u8
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uaddlv"
// LLVM-LABEL: @test_vaddlv_u8
// LLVM: call i32 @llvm.aarch64.neon.uaddlv.i32.v8i8
// OGCG-LABEL: @test_vaddlv_u8
// OGCG: call i32 @llvm.aarch64.neon.uaddlv.i32.v8i8

// vaddlvq_s16
int32_t test_vaddlvq_s16(int16x8_t a) {
  return __builtin_neon_vaddlvq_s16(a);
}
// CIR-LABEL: @test_vaddlvq_s16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.saddlv"
// LLVM-LABEL: @test_vaddlvq_s16
// LLVM: call i32 @llvm.aarch64.neon.saddlv.i32.v8i16
// OGCG-LABEL: @test_vaddlvq_s16
// OGCG: call i32 @llvm.aarch64.neon.saddlv.i32.v8i16

// vaddlvq_u8
uint16_t test_vaddlvq_u8(uint8x16_t a) {
  return __builtin_neon_vaddlvq_u8(a);
}
// CIR-LABEL: @test_vaddlvq_u8
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uaddlv"
// LLVM-LABEL: @test_vaddlvq_u8
// LLVM: call i32 @llvm.aarch64.neon.uaddlv.i32.v16i8
// OGCG-LABEL: @test_vaddlvq_u8
// OGCG: call i32 @llvm.aarch64.neon.uaddlv.i32.v16i8

// vaddvq_f64
float64_t test_vaddvq_f64(float64x2_t a) {
  return __builtin_neon_vaddvq_f64(a);
}
// CIR-LABEL: @test_vaddvq_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.faddv"
// LLVM-LABEL: @test_vaddvq_f64
// LLVM: call double @llvm.aarch64.neon.faddv.f64.v2f64
// OGCG-LABEL: @test_vaddvq_f64
// OGCG: call double @llvm.aarch64.neon.faddv.f64.v2f64

// vrbit_u8
uint8x8_t test_vrbit_u8(uint8x8_t a) {
  return __builtin_neon_vrbit_v(a, 16);
}
// CIR-LABEL: @test_vrbit_u8
// CIR: cir.call_llvm_intrinsic "bitreverse"
// LLVM-LABEL: @test_vrbit_u8
// LLVM: call <8 x i8> @llvm.bitreverse.v8i8
// OGCG-LABEL: @test_vrbit_u8
// OGCG: call <8 x i8> @llvm.bitreverse.v8i8

// vrbitq_u8
uint8x16_t test_vrbitq_u8(uint8x16_t a) {
  return __builtin_neon_vrbitq_v(a, 48);
}
// CIR-LABEL: @test_vrbitq_u8
// CIR: cir.call_llvm_intrinsic "bitreverse"
// LLVM-LABEL: @test_vrbitq_u8
// LLVM: call <16 x i8> @llvm.bitreverse.v16i8
// OGCG-LABEL: @test_vrbitq_u8
// OGCG: call <16 x i8> @llvm.bitreverse.v16i8

// vmulx_f32
float32x2_t test_vmulx_f32(float32x2_t a, float32x2_t b) {
  return __builtin_neon_vmulx_v(a, b, 9);
}
// CIR-LABEL: @test_vmulx_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmulx"
// LLVM-LABEL: @test_vmulx_f32
// LLVM: call <2 x float> @llvm.aarch64.neon.fmulx.v2f32
// OGCG-LABEL: @test_vmulx_f32
// OGCG: call <2 x float> @llvm.aarch64.neon.fmulx.v2f32

// vmulxq_f32
float32x4_t test_vmulxq_f32(float32x4_t a, float32x4_t b) {
  return __builtin_neon_vmulxq_v(a, b, 41);
}
// CIR-LABEL: @test_vmulxq_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmulx"
// LLVM-LABEL: @test_vmulxq_f32
// LLVM: call <4 x float> @llvm.aarch64.neon.fmulx.v4f32
// OGCG-LABEL: @test_vmulxq_f32
// OGCG: call <4 x float> @llvm.aarch64.neon.fmulx.v4f32

// vmaxv_f32
float32_t test_vmaxv_f32(float32x2_t a) {
  return __builtin_neon_vmaxv_f32(a);
}
// CIR-LABEL: @test_vmaxv_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmaxv"
// LLVM-LABEL: @test_vmaxv_f32
// LLVM: call float @llvm.aarch64.neon.fmaxv.f32.v2f32
// OGCG-LABEL: @test_vmaxv_f32
// OGCG: call float @llvm.aarch64.neon.fmaxv.f32.v2f32

// vminvq_f64
float64_t test_vminvq_f64(float64x2_t a) {
  return __builtin_neon_vminvq_f64(a);
}
// CIR-LABEL: @test_vminvq_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fminv"
// LLVM-LABEL: @test_vminvq_f64
// LLVM: call double @llvm.aarch64.neon.fminv.f64.v2f64
// OGCG-LABEL: @test_vminvq_f64
// OGCG: call double @llvm.aarch64.neon.fminv.f64.v2f64

// vpmaxs_f32
float32_t test_vpmaxs_f32(float32x2_t a) {
  return __builtin_neon_vpmaxs_f32(a);
}
// CIR-LABEL: @test_vpmaxs_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmaxv"
// LLVM-LABEL: @test_vpmaxs_f32
// LLVM: call float @llvm.aarch64.neon.fmaxv.f32.v2f32
// OGCG-LABEL: @test_vpmaxs_f32
// OGCG: call float @llvm.aarch64.neon.fmaxv.f32.v2f32

// vmaxh_f16
__attribute__((target("fullfp16")))
__fp16 test_vmaxh_f16(__fp16 a, __fp16 b) {
  return __builtin_neon_vmaxh_f16(a, b);
}
// CIR-LABEL: @test_vmaxh_f16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmax"
// LLVM-LABEL: @test_vmaxh_f16
// LLVM: call half @llvm.aarch64.neon.fmax.f16(half
// OGCG-LABEL: @test_vmaxh_f16
// OGCG: call half @llvm.aarch64.neon.fmax.f16(half

// vminh_f16
__attribute__((target("fullfp16")))
__fp16 test_vminh_f16(__fp16 a, __fp16 b) {
  return __builtin_neon_vminh_f16(a, b);
}
// CIR-LABEL: @test_vminh_f16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmin"
// LLVM-LABEL: @test_vminh_f16
// LLVM: call half @llvm.aarch64.neon.fmin.f16(half
// OGCG-LABEL: @test_vminh_f16
// OGCG: call half @llvm.aarch64.neon.fmin.f16(half

// vabds_f32
float32_t test_vabds_f32_again(float32_t a, float32_t b) {
  return __builtin_neon_vabds_f32(a, b);
}
// CIR-LABEL: @test_vabds_f32_again
// CIR: cir.call_llvm_intrinsic "aarch64.sisd.fabd"
// LLVM-LABEL: @test_vabds_f32_again
// LLVM: call float @llvm.aarch64.sisd.fabd.f32(float
// OGCG-LABEL: @test_vabds_f32_again
// OGCG: call float @llvm.aarch64.sisd.fabd.f32(float

// vmulxs_f32
float32_t test_vmulxs_f32(float32_t a, float32_t b) {
  return __builtin_neon_vmulxs_f32(a, b);
}
// CIR-LABEL: @test_vmulxs_f32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.fmulx"
// LLVM-LABEL: @test_vmulxs_f32
// LLVM: call float @llvm.aarch64.neon.fmulx.f32(float
// OGCG-LABEL: @test_vmulxs_f32
// OGCG: call float @llvm.aarch64.neon.fmulx.f32(float

// vqabss_s32
int32_t test_vqabss_s32(int32_t a) {
  return __builtin_neon_vqabss_s32(a);
}
// CIR-LABEL: @test_vqabss_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqabs"
// LLVM-LABEL: @test_vqabss_s32
// LLVM: call i32 @llvm.aarch64.neon.sqabs.i32(i32
// OGCG-LABEL: @test_vqabss_s32
// OGCG: call i32 @llvm.aarch64.neon.sqabs.i32(i32

// vqaddd_u64
uint64_t test_vqaddd_u64(uint64_t a, uint64_t b) {
  return __builtin_neon_vqaddd_u64(a, b);
}
// CIR-LABEL: @test_vqaddd_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqadd"
// LLVM-LABEL: @test_vqaddd_u64
// LLVM: call i64 @llvm.aarch64.neon.uqadd.i64(i64
// OGCG-LABEL: @test_vqaddd_u64
// OGCG: call i64 @llvm.aarch64.neon.uqadd.i64(i64

// vqnegs_s32
int32_t test_vqnegs_s32(int32_t a) {
  return __builtin_neon_vqnegs_s32(a);
}
// CIR-LABEL: @test_vqnegs_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqneg"
// LLVM-LABEL: @test_vqnegs_s32
// LLVM: call i32 @llvm.aarch64.neon.sqneg.i32(i32
// OGCG-LABEL: @test_vqnegs_s32
// OGCG: call i32 @llvm.aarch64.neon.sqneg.i32(i32

// vqrdmulhs_s32
int32_t test_vqrdmulhs_s32(int32_t a, int32_t b) {
  return __builtin_neon_vqrdmulhs_s32(a, b);
}
// CIR-LABEL: @test_vqrdmulhs_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrdmulh"
// LLVM-LABEL: @test_vqrdmulhs_s32
// LLVM: call i32 @llvm.aarch64.neon.sqrdmulh.i32(i32
// OGCG-LABEL: @test_vqrdmulhs_s32
// OGCG: call i32 @llvm.aarch64.neon.sqrdmulh.i32(i32

// vqrshld_s64
int64_t test_vqrshld_s64(int64_t a, int64_t b) {
  return __builtin_neon_vqrshld_s64(a, b);
}
// CIR-LABEL: @test_vqrshld_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshl"
// LLVM-LABEL: @test_vqrshld_s64
// LLVM: call i64 @llvm.aarch64.neon.sqrshl.i64(i64
// OGCG-LABEL: @test_vqrshld_s64
// OGCG: call i64 @llvm.aarch64.neon.sqrshl.i64(i64

// vqrshrnd_n_s64
int32_t test_vqrshrnd_n_s64(int64_t a) {
  return __builtin_neon_vqrshrnd_n_s64(a, 16);
}
// CIR-LABEL: @test_vqrshrnd_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshrn"
// LLVM-LABEL: @test_vqrshrnd_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqrshrn.i32(i64
// OGCG-LABEL: @test_vqrshrnd_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqrshrn.i32(i64

// vqrshrns_n_s32
int16_t test_vqrshrns_n_s32(int32_t a) {
  return __builtin_neon_vqrshrns_n_s32(a, 8);
}
// CIR-LABEL: @test_vqrshrns_n_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshrn"
// LLVM-LABEL: @test_vqrshrns_n_s32
// LLVM: call <4 x i16> @llvm.aarch64.neon.sqrshrn.v4i16(<4 x i32>
// OGCG-LABEL: @test_vqrshrns_n_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrshrn.v4i16(<4 x i32>

// vqrshrund_n_s64
uint32_t test_vqrshrund_n_s64(int64_t a) {
  return __builtin_neon_vqrshrund_n_s64(a, 16);
}
// CIR-LABEL: @test_vqrshrund_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshrun"
// LLVM-LABEL: @test_vqrshrund_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqrshrun.i32(i64
// OGCG-LABEL: @test_vqrshrund_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqrshrun.i32(i64

// vqshld_s64
int64_t test_vqshld_s64(int64_t a, int64_t b) {
  return __builtin_neon_vqshld_s64(a, b);
}
// CIR-LABEL: @test_vqshld_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshl"
// LLVM-LABEL: @test_vqshld_s64
// LLVM: call i64 @llvm.aarch64.neon.sqshl.i64(i64
// OGCG-LABEL: @test_vqshld_s64
// OGCG: call i64 @llvm.aarch64.neon.sqshl.i64(i64

// vqshlus_n_s32
uint32_t test_vqshlus_n_s32(int32_t a) {
  return __builtin_neon_vqshlus_n_s32(a, 8);
}
// CIR-LABEL: @test_vqshlus_n_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshlu"
// LLVM-LABEL: @test_vqshlus_n_s32
// LLVM: call i32 @llvm.aarch64.neon.sqshlu.i32(i32
// OGCG-LABEL: @test_vqshlus_n_s32
// OGCG: call i32 @llvm.aarch64.neon.sqshlu.i32(i32

// vqshrnd_n_s64
int32_t test_vqshrnd_n_s64(int64_t a) {
  return __builtin_neon_vqshrnd_n_s64(a, 16);
}
// CIR-LABEL: @test_vqshrnd_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshrn"
// LLVM-LABEL: @test_vqshrnd_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqshrn.i32(i64
// OGCG-LABEL: @test_vqshrnd_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqshrn.i32(i64

// vqsubd_u64
uint64_t test_vqsubd_u64(uint64_t a, uint64_t b) {
  return __builtin_neon_vqsubd_u64(a, b);
}
// CIR-LABEL: @test_vqsubd_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqsub"
// LLVM-LABEL: @test_vqsubd_u64
// LLVM: call i64 @llvm.aarch64.neon.uqsub.i64(i64
// OGCG-LABEL: @test_vqsubd_u64
// OGCG: call i64 @llvm.aarch64.neon.uqsub.i64(i64

// vqshrns_n_s32
int16_t test_vqshrns_n_s32(int32_t a) {
  return __builtin_neon_vqshrns_n_s32(a, 8);
}
// CIR-LABEL: @test_vqshrns_n_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshrn"
// LLVM-LABEL: @test_vqshrns_n_s32
// LLVM: call <4 x i16> @llvm.aarch64.neon.sqshrn.v4i16(<4 x i32>
// OGCG-LABEL: @test_vqshrns_n_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshrn.v4i16(<4 x i32>

// vqshrund_n_s64
uint32_t test_vqshrund_n_s64(int64_t a) {
  return __builtin_neon_vqshrund_n_s64(a, 16);
}
// CIR-LABEL: @test_vqshrund_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshrun"
// LLVM-LABEL: @test_vqshrund_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqshrun.i32(i64
// OGCG-LABEL: @test_vqshrund_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqshrun.i32(i64

// vrecped_f64
float64_t test_vrecped_f64(float64_t a) {
  return __builtin_neon_vrecped_f64(a);
}
// CIR-LABEL: @test_vrecped_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frecpe"
// LLVM-LABEL: @test_vrecped_f64
// LLVM: call double @llvm.aarch64.neon.frecpe.f64(double
// OGCG-LABEL: @test_vrecped_f64
// OGCG: call double @llvm.aarch64.neon.frecpe.f64(double

// vrecpxd_f64
float64_t test_vrecpxd_f64(float64_t a) {
  return __builtin_neon_vrecpxd_f64(a);
}
// CIR-LABEL: @test_vrecpxd_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frecpx"
// LLVM-LABEL: @test_vrecpxd_f64
// LLVM: call double @llvm.aarch64.neon.frecpx.f64(double
// OGCG-LABEL: @test_vrecpxd_f64
// OGCG: call double @llvm.aarch64.neon.frecpx.f64(double

// vrshld_s64
int64_t test_vrshld_s64(int64_t a, int64_t b) {
  return __builtin_neon_vrshld_s64(a, b);
}
// CIR-LABEL: @test_vrshld_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.srshl"
// LLVM-LABEL: @test_vrshld_s64
// LLVM: call i64 @llvm.aarch64.neon.srshl.i64(i64
// OGCG-LABEL: @test_vrshld_s64
// OGCG: call i64 @llvm.aarch64.neon.srshl.i64(i64

// vrshld_u64
uint64_t test_vrshld_u64(uint64_t a, int64_t b) {
  return __builtin_neon_vrshld_u64(a, b);
}
// CIR-LABEL: @test_vrshld_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.urshl"
// LLVM-LABEL: @test_vrshld_u64
// LLVM: call i64 @llvm.aarch64.neon.urshl.i64(i64
// OGCG-LABEL: @test_vrshld_u64
// OGCG: call i64 @llvm.aarch64.neon.urshl.i64(i64

// vrsqrted_f64
float64_t test_vrsqrted_f64(float64_t a) {
  return __builtin_neon_vrsqrted_f64(a);
}
// CIR-LABEL: @test_vrsqrted_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frsqrte"
// LLVM-LABEL: @test_vrsqrted_f64
// LLVM: call double @llvm.aarch64.neon.frsqrte.f64(double
// OGCG-LABEL: @test_vrsqrted_f64
// OGCG: call double @llvm.aarch64.neon.frsqrte.f64(double

// vrsqrtsd_f64
float64_t test_vrsqrtsd_f64(float64_t a, float64_t b) {
  return __builtin_neon_vrsqrtsd_f64(a, b);
}
// CIR-LABEL: @test_vrsqrtsd_f64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.frsqrts"
// LLVM-LABEL: @test_vrsqrtsd_f64
// LLVM: call double @llvm.aarch64.neon.frsqrts.f64(double
// OGCG-LABEL: @test_vrsqrtsd_f64
// OGCG: call double @llvm.aarch64.neon.frsqrts.f64(double

// vshld_s64
int64_t test_vshld_s64(int64_t a, int64_t b) {
  return __builtin_neon_vshld_s64(a, b);
}
// CIR-LABEL: @test_vshld_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sshl"
// LLVM-LABEL: @test_vshld_s64
// LLVM: call i64 @llvm.aarch64.neon.sshl.i64(i64
// OGCG-LABEL: @test_vshld_s64
// OGCG: call i64 @llvm.aarch64.neon.sshl.i64(i64

// vshld_u64
uint64_t test_vshld_u64(uint64_t a, int64_t b) {
  return __builtin_neon_vshld_u64(a, b);
}
// CIR-LABEL: @test_vshld_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ushl"
// LLVM-LABEL: @test_vshld_u64
// LLVM: call i64 @llvm.aarch64.neon.ushl.i64(i64
// OGCG-LABEL: @test_vshld_u64
// OGCG: call i64 @llvm.aarch64.neon.ushl.i64(i64

// vsqaddd_u64
uint64_t test_vsqaddd_u64(uint64_t a, int64_t b) {
  return __builtin_neon_vsqaddd_u64(a, b);
}
// CIR-LABEL: @test_vsqaddd_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.usqadd"
// LLVM-LABEL: @test_vsqaddd_u64
// LLVM: call i64 @llvm.aarch64.neon.usqadd.i64(i64
// OGCG-LABEL: @test_vsqaddd_u64
// OGCG: call i64 @llvm.aarch64.neon.usqadd.i64(i64

// vuqaddd_s64
int64_t test_vuqaddd_s64(int64_t a, uint64_t b) {
  return __builtin_neon_vuqaddd_s64(a, b);
}
// CIR-LABEL: @test_vuqaddd_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.suqadd"
// LLVM-LABEL: @test_vuqaddd_s64
// LLVM: call i64 @llvm.aarch64.neon.suqadd.i64(i64
// OGCG-LABEL: @test_vuqaddd_s64
// OGCG: call i64 @llvm.aarch64.neon.suqadd.i64(i64

// vqrdmlahh_s16
__attribute__((target("v8.1a,neon")))
int16_t test_vqrdmlahh_s16(int16_t a, int16_t b, int16_t c) {
  return __builtin_neon_vqrdmlahh_s16(a, b, c);
}
// CIR-LABEL: @test_vqrdmlahh_s16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrdmlah"
// LLVM-LABEL: @test_vqrdmlahh_s16
// LLVM: call <4 x i16> @llvm.aarch64.neon.sqrdmlah.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrdmlahh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrdmlah.v4i16(<4 x i16>

// vqrdmlshh_s16
__attribute__((target("v8.1a,neon")))
int16_t test_vqrdmlshh_s16(int16_t a, int16_t b, int16_t c) {
  return __builtin_neon_vqrdmlshh_s16(a, b, c);
}
// CIR-LABEL: @test_vqrdmlshh_s16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrdmlsh"
// LLVM-LABEL: @test_vqrdmlshh_s16
// LLVM: call <4 x i16> @llvm.aarch64.neon.sqrdmlsh.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrdmlshh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrdmlsh.v4i16(<4 x i16>

// vqrdmulhh_s16
__attribute__((target("v8.1a,neon")))
int16_t test_vqrdmulhh_s16(int16_t a, int16_t b) {
  return __builtin_neon_vqrdmulhh_s16(a, b);
}
// CIR-LABEL: @test_vqrdmulhh_s16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrdmulh"
// LLVM-LABEL: @test_vqrdmulhh_s16
// LLVM: call <4 x i16> @llvm.aarch64.neon.sqrdmulh.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrdmulhh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrdmulh.v4i16(<4 x i16>

// vqrshlb_s8
signed char test_vqrshlb_s8(signed char a, signed char b) {
  return __builtin_neon_vqrshlb_s8(a, b);
}
// CIR-LABEL: @test_vqrshlb_s8
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshl"
// LLVM-LABEL: @test_vqrshlb_s8
// LLVM: call <8 x i8> @llvm.aarch64.neon.sqrshl.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqrshlb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqrshl.v8i8(<8 x i8>

// vqrshlb_u8
unsigned char test_vqrshlb_u8(unsigned char a, signed char b) {
  return __builtin_neon_vqrshlb_u8(a, b);
}
// CIR-LABEL: @test_vqrshlb_u8
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqrshl"
// LLVM-LABEL: @test_vqrshlb_u8
// LLVM: call <8 x i8> @llvm.aarch64.neon.uqrshl.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqrshlb_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqrshl.v8i8(<8 x i8>

// vqrshlh_s16
int16_t test_vqrshlh_s16(int16_t a, int16_t b) {
  return __builtin_neon_vqrshlh_s16(a, b);
}
// CIR-LABEL: @test_vqrshlh_s16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshl"
// LLVM-LABEL: @test_vqrshlh_s16
// LLVM: call <4 x i16> @llvm.aarch64.neon.sqrshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrshlh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrshl.v4i16(<4 x i16>

// vqrshlh_u16
unsigned short test_vqrshlh_u16(unsigned short a, int16_t b) {
  return __builtin_neon_vqrshlh_u16(a, b);
}
// CIR-LABEL: @test_vqrshlh_u16
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqrshl"
// LLVM-LABEL: @test_vqrshlh_u16
// LLVM: call <4 x i16> @llvm.aarch64.neon.uqrshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrshlh_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqrshl.v4i16(<4 x i16>
