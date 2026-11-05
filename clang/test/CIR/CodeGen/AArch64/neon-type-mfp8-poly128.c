// RUN: %clang_cc1 -triple aarch64-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression test: GetNeonType() previously crashed with llvm_unreachable
// for NeonTypeFlags::MFloat8 and NeonTypeFlags::Poly128.

#include <arm_neon.h>

// MFloat8 uses <8 x i8> vector representation, same as Int8/Poly8.
void test_mfp8_load(const __mfp8 *p) {
  mfloat8x8_t v = vld1_mf8(p);
}

// CHECK-LABEL: define {{.*}} @test_mfp8_load(
// CHECK: load <8 x i8>, ptr
// CHECK: ret void

// OGCG-LABEL: define {{.*}} @test_mfp8_load(
// OGCG: load <8 x i8>, ptr
// OGCG: ret void

// Poly128 uses v16i8 representation as a workaround for missing i128 support.
poly128_t test_poly128_load(poly128_t *p) {
  return vldrq_p128(p);
}

// CHECK-LABEL: define {{.*}} @test_poly128_load(
// CHECK: load i128, ptr
// CHECK: ret i128

// OGCG-LABEL: define {{.*}} @test_poly128_load(
// OGCG: load i128, ptr
// OGCG: ret i128
