// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon -target-feature +lut -disable-O0-optnone -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-flat -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// REQUIRES: aarch64-registered-target

#include <arm_neon.h>

// CHECK-LABEL: cir.func {{.*}} @test_vdup_n_mf8(
// CHECK-SAME: %arg0: !cir.vector<1 x !s8i>{{.*}}) -> !cir.vector<8 x !s8i>
// CHECK: %[[ARG0:.+]] = cir.load align(1) %{{.+}} : !cir.ptr<!cir.vector<1 x !s8i>>, !cir.vector<1 x !s8i>
// CHECK: %[[CALL0:.+]] = cir.call @vdup_n_mf8(%[[ARG0]]) : (!cir.vector<1 x !s8i>) -> !cir.vector<8 x !s8i>
// CHECK: cir.return %[[CALL0]] : !cir.vector<8 x !s8i>
mfloat8x8_t test_vdup_n_mf8(mfloat8_t a) {
  return vdup_n_mf8(a);
}

// CHECK-LABEL: cir.func {{.*}} @test_vdupq_n_mf8(
// CHECK-SAME: %arg0: !cir.vector<1 x !s8i>{{.*}}) -> !cir.vector<16 x !s8i>
// CHECK: %[[ARG1:.+]] = cir.load align(1) %{{.+}} : !cir.ptr<!cir.vector<1 x !s8i>>, !cir.vector<1 x !s8i>
// CHECK: %[[CALL1:.+]] = cir.call @vdupq_n_mf8(%[[ARG1]]) : (!cir.vector<1 x !s8i>) -> !cir.vector<16 x !s8i>
// CHECK: cir.return %[[CALL1]] : !cir.vector<16 x !s8i>
mfloat8x16_t test_vdupq_n_mf8(mfloat8_t a) {
  return vdupq_n_mf8(a);
}

// CHECK-LABEL: cir.func {{.*}} @test_vmov_n_mf8(
// CHECK-SAME: %arg0: !cir.vector<1 x !s8i>{{.*}}) -> !cir.vector<8 x !s8i>
// CHECK: %[[ARG2:.+]] = cir.load align(1) %{{.+}} : !cir.ptr<!cir.vector<1 x !s8i>>, !cir.vector<1 x !s8i>
// CHECK: %[[CALL2:.+]] = cir.call @vmov_n_mf8(%[[ARG2]]) : (!cir.vector<1 x !s8i>) -> !cir.vector<8 x !s8i>
// CHECK: cir.return %[[CALL2]] : !cir.vector<8 x !s8i>
mfloat8x8_t test_vmov_n_mf8(mfloat8_t a) {
  return vmov_n_mf8(a);
}

// CHECK-LABEL: cir.func {{.*}} @test_vmovq_n_mf8(
// CHECK-SAME: %arg0: !cir.vector<1 x !s8i>{{.*}}) -> !cir.vector<16 x !s8i>
// CHECK: %[[ARG3:.+]] = cir.load align(1) %{{.+}} : !cir.ptr<!cir.vector<1 x !s8i>>, !cir.vector<1 x !s8i>
// CHECK: %[[CALL3:.+]] = cir.call @vmovq_n_mf8(%[[ARG3]]) : (!cir.vector<1 x !s8i>) -> !cir.vector<16 x !s8i>
// CHECK: cir.return %[[CALL3]] : !cir.vector<16 x !s8i>
mfloat8x16_t test_vmovq_n_mf8(mfloat8_t a) {
  return vmovq_n_mf8(a);
}
