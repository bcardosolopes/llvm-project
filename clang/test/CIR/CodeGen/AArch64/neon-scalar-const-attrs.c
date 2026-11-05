// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +fullfp16 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_fp16.h>
#include <arm_neon.h>

// CIR-LABEL: @test_vceqd_f64(
// CIR: fcmp oeq double {{.*}}, {{.*}}
// CIR: select i1 {{.*}}, i64 -1, i64 0
// OGCG-LABEL: @test_vceqd_f64(
// OGCG: fcmp oeq double {{.*}}, {{.*}}
// OGCG: sext i1 {{.*}} to i64
uint64_t test_vceqd_f64(float64_t a, float64_t b) { return vceqd_f64(a, b); }

// CIR-LABEL: @test_vceqs_f32(
// CIR: fcmp oeq float {{.*}}, {{.*}}
// CIR: select i1 {{.*}}, i32 -1, i32 0
// OGCG-LABEL: @test_vceqs_f32(
// OGCG: fcmp oeq float {{.*}}, {{.*}}
// OGCG: sext i1 {{.*}} to i32
uint32_t test_vceqs_f32(float32_t a, float32_t b) { return vceqs_f32(a, b); }

// CIR-LABEL: @test_vceqh_f16(
// CIR: fcmp oeq half {{.*}}, {{.*}}
// CIR: select i1 {{.*}}, i16 -1, i16 0
// OGCG-LABEL: @test_vceqh_f16(
// OGCG: fcmp oeq half {{.*}}, {{.*}}
// OGCG: sext i1 {{.*}} to i16
uint16_t test_vceqh_f16(float16_t a, float16_t b) { return vceqh_f16(a, b); }

// CIR-LABEL: @test_vtstd_s64(
// CIR: and i64 {{.*}}, {{.*}}
// CIR: icmp ne i64 {{.*}}, 0
// CIR: select i1 {{.*}}, i64 -1, i64 0
// OGCG-LABEL: @test_vtstd_s64(
// OGCG: and i64 {{.*}}, {{.*}}
// OGCG: icmp ne i64 {{.*}}, 0
// OGCG: sext i1 {{.*}} to i64
uint64_t test_vtstd_s64(int64_t a, int64_t b) { return vtstd_s64(a, b); }
