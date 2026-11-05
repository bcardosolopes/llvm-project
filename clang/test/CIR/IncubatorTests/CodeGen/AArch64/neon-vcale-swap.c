// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Verify that vcale/vcalt SISD builtins swap operands before calling
// facge/facgt, matching OGCG.

#include <arm_neon.h>

// CIR-LABEL: @test_vcales_f32
// OGCG-LABEL: @test_vcales_f32
uint32_t test_vcales_f32(float a, float b) {
  return vcales_f32(a, b);
}
// vcale(a,b) = |a| <= |b| = facge(b,a): second arg should come first
// CIR: call i32 @llvm.aarch64.neon.facge.i32.f32(float %{{.*}}, float %{{.*}})
// OGCG: call i32 @llvm.aarch64.neon.facge.i32.f32(float %{{.*}}, float %{{.*}})

// CIR-LABEL: @test_vcaled_f64
// OGCG-LABEL: @test_vcaled_f64
uint64_t test_vcaled_f64(double a, double b) {
  return vcaled_f64(a, b);
}
// CIR: call i64 @llvm.aarch64.neon.facge.i64.f64(double %{{.*}}, double %{{.*}})
// OGCG: call i64 @llvm.aarch64.neon.facge.i64.f64(double %{{.*}}, double %{{.*}})

// CIR-LABEL: @test_vcalts_f32
// OGCG-LABEL: @test_vcalts_f32
uint32_t test_vcalts_f32(float a, float b) {
  return vcalts_f32(a, b);
}
// CIR: call i32 @llvm.aarch64.neon.facgt.i32.f32(float %{{.*}}, float %{{.*}})
// OGCG: call i32 @llvm.aarch64.neon.facgt.i32.f32(float %{{.*}}, float %{{.*}})

// CIR-LABEL: @test_vcaltd_f64
// OGCG-LABEL: @test_vcaltd_f64
uint64_t test_vcaltd_f64(double a, double b) {
  return vcaltd_f64(a, b);
}
// CIR: call i64 @llvm.aarch64.neon.facgt.i64.f64(double %{{.*}}, double %{{.*}})
// OGCG: call i64 @llvm.aarch64.neon.facgt.i64.f64(double %{{.*}}, double %{{.*}})
