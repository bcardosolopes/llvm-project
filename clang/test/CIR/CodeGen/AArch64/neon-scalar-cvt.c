// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// CIR-LABEL: @test_vcvts_f32_s32
// CIR: sitofp i32 %{{.*}} to float
// CIR: ret float
// OGCG-LABEL: @test_vcvts_f32_s32
// OGCG: sitofp i32 %{{.*}} to float
// OGCG: ret float
float test_vcvts_f32_s32(int32_t a) {
  return vcvts_f32_s32(a);
}

// CIR-LABEL: @test_vcvts_f32_u32
// CIR: uitofp i32 %{{.*}} to float
// CIR: ret float
// OGCG-LABEL: @test_vcvts_f32_u32
// OGCG: uitofp i32 %{{.*}} to float
// OGCG: ret float
float test_vcvts_f32_u32(uint32_t a) {
  return vcvts_f32_u32(a);
}

// CIR-LABEL: @test_vcvtd_f64_s64
// CIR: sitofp i64 %{{.*}} to double
// CIR: ret double
// OGCG-LABEL: @test_vcvtd_f64_s64
// OGCG: sitofp i64 %{{.*}} to double
// OGCG: ret double
double test_vcvtd_f64_s64(int64_t a) {
  return vcvtd_f64_s64(a);
}

// CIR-LABEL: @test_vcvtd_f64_u64
// CIR: uitofp i64 %{{.*}} to double
// CIR: ret double
// OGCG-LABEL: @test_vcvtd_f64_u64
// OGCG: uitofp i64 %{{.*}} to double
// OGCG: ret double
double test_vcvtd_f64_u64(uint64_t a) {
  return vcvtd_f64_u64(a);
}
