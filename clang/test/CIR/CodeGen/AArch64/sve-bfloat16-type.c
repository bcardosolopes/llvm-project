// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -target-feature +bf16 \
// RUN:   -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir
//
// Verify that SVE bfloat16 vectors use !cir.bf16 element type, not !cir.f16.

typedef __SVBfloat16_t svbfloat16_t;
typedef __SVFloat16_t svfloat16_t;

// CHECK-LABEL: @test_svbfloat16
// CHECK: !cir.vector<[8] x !cir.bf16>
svbfloat16_t test_svbfloat16(svbfloat16_t a) {
  return a;
}

// CHECK-LABEL: @test_svfloat16
// CHECK: !cir.vector<[8] x !cir.f16>
svfloat16_t test_svfloat16(svfloat16_t a) {
  return a;
}
