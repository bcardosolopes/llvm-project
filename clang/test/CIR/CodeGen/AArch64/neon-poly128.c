// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vaddq_p128
// CIR: xor i128
// CIR: ret i128
// OGCG-LABEL: @test_vaddq_p128
// OGCG: xor
// OGCG: ret i128
poly128_t test_vaddq_p128(poly128_t a, poly128_t b) {
  return vaddq_p128(a, b);
}

// CIR-LABEL: @test_vldrq_p128
// CIR: load i128, ptr %{{.*}}, align 16
// CIR: ret i128
// OGCG-LABEL: @test_vldrq_p128
// OGCG: load i128, ptr %{{.*}}, align 16
// OGCG: ret i128
poly128_t test_vldrq_p128(const poly128_t *p) {
  return vldrq_p128(p);
}

// CIR-LABEL: @test_vstrq_p128
// CIR: store i128 %{{.*}}, ptr %{{.*}}, align 16
// CIR: ret void
// OGCG-LABEL: @test_vstrq_p128
// OGCG: store i128 %{{.*}}, ptr %{{.*}}, align 16
// OGCG: ret void
void test_vstrq_p128(poly128_t *p, poly128_t val) {
  vstrq_p128(p, val);
}
