// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>
#include <arm_fp16.h>

// CIR-LABEL: @test_vcvth_f16_s16
// CIR: sitofp i16 %{{.*}} to half
// CIR: ret half
// OGCG-LABEL: @test_vcvth_f16_s16
// OGCG: sitofp i16 %{{.*}} to half
// OGCG: ret half
float16_t test_vcvth_f16_s16(int16_t a) {
  return vcvth_f16_s16(a);
}

// CIR-LABEL: @test_vcvth_f16_u16
// CIR: uitofp i16 %{{.*}} to half
// CIR: ret half
// OGCG-LABEL: @test_vcvth_f16_u16
// OGCG: uitofp i16 %{{.*}} to half
// OGCG: ret half
float16_t test_vcvth_f16_u16(uint16_t a) {
  return vcvth_f16_u16(a);
}

// CIR-LABEL: @test_vcvth_f16_s32
// CIR: sitofp i32 %{{.*}} to half
// CIR: ret half
// OGCG-LABEL: @test_vcvth_f16_s32
// OGCG: sitofp i32 %{{.*}} to half
// OGCG: ret half
float16_t test_vcvth_f16_s32(int32_t a) {
  return vcvth_f16_s32(a);
}

// CIR-LABEL: @test_vcvth_f16_u32
// CIR: uitofp i32 %{{.*}} to half
// CIR: ret half
// OGCG-LABEL: @test_vcvth_f16_u32
// OGCG: uitofp i32 %{{.*}} to half
// OGCG: ret half
float16_t test_vcvth_f16_u32(uint32_t a) {
  return vcvth_f16_u32(a);
}

// CIR-LABEL: @test_vcvth_f16_s64
// CIR: sitofp i64 %{{.*}} to half
// CIR: ret half
// OGCG-LABEL: @test_vcvth_f16_s64
// OGCG: sitofp i64 %{{.*}} to half
// OGCG: ret half
float16_t test_vcvth_f16_s64(int64_t a) {
  return vcvth_f16_s64(a);
}

// CIR-LABEL: @test_vcvth_f16_u64
// CIR: uitofp i64 %{{.*}} to half
// CIR: ret half
// OGCG-LABEL: @test_vcvth_f16_u64
// OGCG: uitofp i64 %{{.*}} to half
// OGCG: ret half
float16_t test_vcvth_f16_u64(uint64_t a) {
  return vcvth_f16_u64(a);
}

// CIR-LABEL: @test_vnegh_f16
// CIR: fneg half
// CIR: ret half
// OGCG-LABEL: @test_vnegh_f16
// OGCG: fneg half
// OGCG: ret half
float16_t test_vnegh_f16(float16_t a) {
  return vnegh_f16(a);
}

// CIR-LABEL: @test_vsqrth_f16
// CIR: call half @llvm.sqrt.f16(half
// CIR: ret half
// OGCG-LABEL: @test_vsqrth_f16
// OGCG: call half @llvm.sqrt.f16(half
// OGCG: ret half
float16_t test_vsqrth_f16(float16_t a) {
  return vsqrth_f16(a);
}
