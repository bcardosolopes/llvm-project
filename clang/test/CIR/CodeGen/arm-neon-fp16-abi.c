// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fp-armv8 -fclangir -disable-O0-optnone \
// RUN:    -emit-mlir=cir-abi -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fp-armv8 -fclangir -disable-O0-optnone \
// RUN:    -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fp-armv8 -disable-O0-optnone \
// RUN:    -emit-llvm -o %t-og.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-og.ll %s

// RUN: %clang_cc1 -triple armv7-none-linux-gnueabihf -target-feature +neon \
// RUN:    -target-feature +fullfp16 -fclangir -disable-O0-optnone \
// RUN:    -emit-llvm -o %t-fp16.ll %s
// RUN: FileCheck --check-prefix=LLVM-FP16 --input-file=%t-fp16.ll %s

// On an AArch32 target WITHOUT a legal half type (baseline +fp-armv8, no
// +fullfp16), __fp16 NEON vectors must be handled exactly as classic CodeGen
// (OGCG) does, otherwise the ARM backend cannot legalize <N x half> operands to
// the arm.neon.vld1/vst1 intrinsics (it crashes) and, more subtly, a bare
// float16xN_t argument/return is passed in different registers than AAPCS-VFP:
//
//   1. The arm.neon.vld1/vst1 intrinsic operand/result element type must be
//      i16, not half (CIRGen GetNeonType HasLegalHalfType branch).
//   2. A by-value __fp16 vector parameter/return must be coerced to an integer
//      vector (<2 x i32> for 64-bit, <4 x i32> for 128-bit) at the ABI boundary
//      (CIR ARM ABIInfo isIllegalVectorType/coerceIllegalVector).
//
// With +fullfp16 the half type is legal, so the vectors are kept as <N x half>.

typedef __attribute__((neon_vector_type(4))) __fp16 f16x4;
typedef __attribute__((neon_vector_type(8))) __fp16 f16x8;

// NeonTypeFlags: Float16 = 8, Float16-quad = 40.

// A bare 64-bit __fp16 vector is coerced to <2 x i32> in argument and return
// position, matching AAPCS-VFP (the value stays in a single D register).
f16x4 test_fp16_abi(f16x4 a) { return a; }
// CIR: cir.func{{.*}}@test_fp16_abi(%arg0: !cir.vector<2 x !u32i>{{.*}}-> !cir.vector<2 x !u32i>
// LLVM: define {{.*}}<2 x i32> @test_fp16_abi(<2 x i32>
// OGCG: define {{.*}}<2 x i32> @test_fp16_abi(<2 x i32>
// LLVM-FP16: define {{.*}}<4 x half> @test_fp16_abi(<4 x half>

// A 128-bit __fp16 vector is coerced to <4 x i32>.
f16x8 test_fp16q_abi(f16x8 a) { return a; }
// LLVM: define {{.*}}<4 x i32> @test_fp16q_abi(<4 x i32>
// OGCG: define {{.*}}<4 x i32> @test_fp16q_abi(<4 x i32>
// LLVM-FP16: define {{.*}}<8 x half> @test_fp16q_abi(<8 x half>

// vld1 of a __fp16 vector uses the i16-element intrinsic, not v4f16.
f16x4 test_vld1_f16(const __fp16 *p) {
  return __builtin_neon_vld1_v(p, 8);
}
// CIR-LABEL: @test_vld1_f16
// CIR: cir.call_llvm_intrinsic "arm.neon.vld1"{{.*}} -> !cir.vector<4 x !s16i>
// LLVM-LABEL: @test_vld1_f16
// LLVM: call <4 x i16> @llvm.arm.neon.vld1.v4i16.p0(ptr {{.*}}, i32 2)
// OGCG-LABEL: @test_vld1_f16
// OGCG: call <4 x i16> @llvm.arm.neon.vld1.v4i16.p0(ptr {{.*}}, i32 2)

f16x8 test_vld1q_f16(const __fp16 *p) {
  return __builtin_neon_vld1q_v(p, 40);
}
// LLVM-LABEL: @test_vld1q_f16
// LLVM: call <8 x i16> @llvm.arm.neon.vld1.v8i16.p0(ptr {{.*}}, i32 2)
// OGCG-LABEL: @test_vld1q_f16
// OGCG: call <8 x i16> @llvm.arm.neon.vld1.v8i16.p0(ptr {{.*}}, i32 2)

// vst1 of a __fp16 vector uses the i16-element intrinsic, not v4f16.
void test_vst1_f16(__fp16 *p, f16x4 v) {
  __builtin_neon_vst1_v(p, v, 8);
}
// CIR-LABEL: @test_vst1_f16
// CIR: cir.call_llvm_intrinsic "arm.neon.vst1"{{.*}}!cir.vector<4 x !s16i>
// LLVM-LABEL: @test_vst1_f16
// LLVM: call void @llvm.arm.neon.vst1.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, i32 2)
// OGCG-LABEL: @test_vst1_f16
// OGCG: call void @llvm.arm.neon.vst1.p0.v4i16(ptr {{.*}}, <4 x i16> {{.*}}, i32 2)
