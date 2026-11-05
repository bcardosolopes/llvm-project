// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

#include <arm_neon.h>

// FP16 absolute compare intrinsics previously crashed because
// castVecOfFPTypeToVecOfIntWithSameWidth did not handle FP16 element types.

// CIR-LABEL: @test_vcageq_f16
// CIR: call <8 x i16> @llvm.aarch64.neon.facge.v8i16.v8f16(<8 x half>
// OGCG-LABEL: @test_vcageq_f16
// OGCG: call <8 x i16> @llvm.aarch64.neon.facge.v8i16.v8f16(<8 x half>
uint16x8_t test_vcageq_f16(float16x8_t a, float16x8_t b) {
  return vcageq_f16(a, b);
}

// CIR-LABEL: @test_vcaltq_f16
// CIR: call <8 x i16> @llvm.aarch64.neon.facgt.v8i16.v8f16(<8 x half>
// OGCG-LABEL: @test_vcaltq_f16
// OGCG: call <8 x i16> @llvm.aarch64.neon.facgt.v8i16.v8f16(<8 x half>
uint16x8_t test_vcaltq_f16(float16x8_t a, float16x8_t b) {
  return vcaltq_f16(a, b);
}
