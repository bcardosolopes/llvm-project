// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vmull_s8
// CIR: call <8 x i16> @llvm.aarch64.neon.smull.v8i16(<8 x i8>
// OGCG-LABEL: @test_vmull_s8
// OGCG: call <8 x i16> @llvm.aarch64.neon.smull.v8i16(<8 x i8>
int16x8_t test_vmull_s8(int8x8_t a, int8x8_t b) {
  return vmull_s8(a, b);
}

// CIR-LABEL: @test_vmull_u8
// CIR: call <8 x i16> @llvm.aarch64.neon.umull.v8i16(<8 x i8>
// OGCG-LABEL: @test_vmull_u8
// OGCG: call <8 x i16> @llvm.aarch64.neon.umull.v8i16(<8 x i8>
uint16x8_t test_vmull_u8(uint8x8_t a, uint8x8_t b) {
  return vmull_u8(a, b);
}

// CIR-LABEL: @test_vmull_p8
// CIR: call <8 x i16> @llvm.aarch64.neon.pmull.v8i16(<8 x i8>
// OGCG-LABEL: @test_vmull_p8
// OGCG: call <8 x i16> @llvm.aarch64.neon.pmull.v8i16(<8 x i8>
poly16x8_t test_vmull_p8(poly8x8_t a, poly8x8_t b) {
  return vmull_p8(a, b);
}

// CIR-LABEL: @test_vpadalq_s8
// CIR: call <8 x i16> @llvm.aarch64.neon.saddlp.v8i16.v16i8(<16 x i8>
// OGCG-LABEL: @test_vpadalq_s8
// OGCG: call <8 x i16> @llvm.aarch64.neon.saddlp.v8i16.v16i8(<16 x i8>
int16x8_t test_vpadalq_s8(int16x8_t a, int8x16_t b) {
  return vpadalq_s8(a, b);
}

// CIR-LABEL: @test_vpadalq_u8
// CIR: call <8 x i16> @llvm.aarch64.neon.uaddlp.v8i16.v16i8(<16 x i8>
// OGCG-LABEL: @test_vpadalq_u8
// OGCG: call <8 x i16> @llvm.aarch64.neon.uaddlp.v8i16.v16i8(<16 x i8>
uint16x8_t test_vpadalq_u8(uint16x8_t a, uint8x16_t b) {
  return vpadalq_u8(a, b);
}
