// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +neon -fclangir -emit-llvm -O2 -o - %s | FileCheck %s

// AArch32 NEON lane extract/insert (vget[q]_lane / vset[q]_lane). These were
// previously NYI on 32-bit ARM (only implemented in emitAArch64BuiltinExpr);
// emitARMBuiltinExpr now lowers them to the same target-independent vector
// extract/insert as the AArch64 path.

#include <arm_neon.h>

// CHECK-LABEL: define{{.*}}@getq(
// CHECK: extractelement <4 x i32> %{{.*}}, i64 2
int32_t getq(int32x4_t v) { return vgetq_lane_s32(v, 2); }

// CHECK-LABEL: define{{.*}}@setq(
// CHECK: insertelement <4 x i32> %{{.*}}, i32 99, i64 1
int32x4_t setq(int32x4_t v) { return vsetq_lane_s32(99, v, 1); }

// CHECK-LABEL: define{{.*}}@getd8(
// CHECK: extractelement <8 x i8> %{{.*}}, i64 3
int8_t getd8(int8x8_t v) { return vget_lane_s8(v, 3); }

// CHECK-LABEL: define{{.*}}@seth16(
// CHECK: insertelement <4 x i16> %{{.*}}, i16 7, i64 1
int16x4_t seth16(int16x4_t v) { return vset_lane_s16(7, v, 1); }

// CHECK-LABEL: define{{.*}}@getf32(
// CHECK: extractelement <2 x float> %{{.*}}, i64 1
float32_t getf32(float32x2_t v) { return vget_lane_f32(v, 1); }
