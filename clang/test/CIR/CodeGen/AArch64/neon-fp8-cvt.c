// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon -target-feature +bf16 -target-feature +fp8 -disable-O0-optnone -fclangir -emit-llvm -o - %s | opt -S -p mem2reg,sroa | FileCheck %s
// RUN: %clang_cc1 -x c++ -triple aarch64-none-linux-gnu -target-feature +neon -target-feature +bf16 -target-feature +fp8 -disable-O0-optnone -fclangir -emit-llvm -o /dev/null %s

// REQUIRES: aarch64-registered-target

#include <arm_neon.h>

// CHECK-LABEL: @test_vcvt1_low_bf16_mf8_fpm(
// CHECK: %[[LOW:.*]] = call <8 x i8> @llvm.vector.extract.v8i8.v16i8(<16 x i8> %{{.*}}, i64 0)
// CHECK: call void @llvm.aarch64.set.fpmr(i64 %{{.*}})
// CHECK: %[[CVT:.*]] = call <8 x bfloat> @llvm.aarch64.neon.fp8.cvtl1.v8bf16.v8i8(<8 x i8> %[[LOW]])
// CHECK: ret <8 x bfloat> %[[CVT]]
bfloat16x8_t test_vcvt1_low_bf16_mf8_fpm(mfloat8x16_t op, fpm_t fpm) {
  return vcvt1_low_bf16_mf8_fpm(op, fpm);
}

// CHECK-LABEL: @test_vcvt2_bf16_mf8_fpm(
// CHECK: call void @llvm.aarch64.set.fpmr(i64 %{{.*}})
// CHECK: %[[CVT:.*]] = call <8 x bfloat> @llvm.aarch64.neon.fp8.cvtl2.v8bf16.v8i8(<8 x i8> %{{.*}})
// CHECK: ret <8 x bfloat> %[[CVT]]
bfloat16x8_t test_vcvt2_bf16_mf8_fpm(mfloat8x8_t op, fpm_t fpm) {
  return vcvt2_bf16_mf8_fpm(op, fpm);
}

// CHECK-LABEL: @test_vcvt2_high_f16_mf8_fpm(
// CHECK: call void @llvm.aarch64.set.fpmr(i64 %{{.*}})
// CHECK: %[[CVT:.*]] = call <8 x half> @llvm.aarch64.neon.fp8.cvtl2.v8f16.v16i8(<16 x i8> %{{.*}})
// CHECK: ret <8 x half> %[[CVT]]
float16x8_t test_vcvt2_high_f16_mf8_fpm(mfloat8x16_t op, fpm_t fpm) {
  return vcvt2_high_f16_mf8_fpm(op, fpm);
}

// CHECK-LABEL: @test_vcvt_mf8_f16_fpm(
// CHECK: call void @llvm.aarch64.set.fpmr(i64 %{{.*}})
// CHECK: %[[CVT:.*]] = call <8 x i8> @llvm.aarch64.neon.fp8.fcvtn.v8i8.v4f16(<4 x half> %{{.*}}, <4 x half> %{{.*}})
// CHECK: ret <8 x i8> %[[CVT]]
mfloat8x8_t test_vcvt_mf8_f16_fpm(float16x4_t vn, float16x4_t vm, fpm_t fpm) {
  return vcvt_mf8_f16_fpm(vn, vm, fpm);
}

// CHECK-LABEL: @test_vcvt_mf8_f32_fpm(
// CHECK: call void @llvm.aarch64.set.fpmr(i64 %{{.*}})
// CHECK: %[[CVT:.*]] = call <8 x i8> @llvm.aarch64.neon.fp8.fcvtn.v8i8.v4f32(<4 x float> %{{.*}}, <4 x float> %{{.*}})
// CHECK: ret <8 x i8> %[[CVT]]
mfloat8x8_t test_vcvt_mf8_f32_fpm(float32x4_t vn, float32x4_t vm, fpm_t fpm) {
  return vcvt_mf8_f32_fpm(vn, vm, fpm);
}

// CHECK-LABEL: @test_vcvt_high_mf8_f32_fpm(
// CHECK: %[[WIDE:.*]] = call <16 x i8> @llvm.vector.insert.v16i8.v8i8(<16 x i8> poison, <8 x i8> %{{.*}}, i64 0)
// CHECK: call void @llvm.aarch64.set.fpmr(i64 %{{.*}})
// CHECK: %[[CVT:.*]] = call <16 x i8> @llvm.aarch64.neon.fp8.fcvtn2.v16i8.v4f32(<16 x i8> %[[WIDE]], <4 x float> %{{.*}}, <4 x float> %{{.*}})
// CHECK: ret <16 x i8> %[[CVT]]
mfloat8x16_t test_vcvt_high_mf8_f32_fpm(mfloat8x8_t vd, float32x4_t vn,
                                        float32x4_t vm, fpm_t fpm) {
  return vcvt_high_mf8_f32_fpm(vd, vn, vm, fpm);
}
