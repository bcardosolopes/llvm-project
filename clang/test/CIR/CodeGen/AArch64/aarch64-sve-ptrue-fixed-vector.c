// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -mvscale-min=1 -mvscale-max=1 -O1 -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=CHECK128
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -mvscale-min=1 -mvscale-max=1 -O1 -fclangir -emit-cir %s -o %t.cir
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=CHECK128
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -mvscale-min=2 -mvscale-max=2 -O1 -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=CHECK256

#include <arm_sve.h>

#define N __ARM_FEATURE_SVE_BITS
typedef int8_t vec_int8 __attribute__((vector_size(N / 8)));

// CHECK128-LABEL: define dso_local <16 x i8> @f2(
// CHECK128: call <vscale x 16 x i8> @llvm.vector.insert.nxv16i8.v16i8(
// CHECK128: call <vscale x 16 x i8> @llvm.aarch64.sve.asrd.nxv16i8(
// CHECK128: call <16 x i8> @llvm.vector.extract.v16i8.nxv16i8(
// CHECK128: ret <16 x i8>
//
// CHECK256-LABEL: define dso_local void @f2(
// CHECK256-SAME: ptr dead_on_unwind noalias writable writeonly sret(<32 x i8>) align 16
// CHECK256-SAME: ptr dead_on_return noundef readonly
// CHECK256: load <32 x i8>, ptr {{.*}}, align 16
// CHECK256: call <vscale x 16 x i8> @llvm.vector.insert.nxv16i8.v32i8(
// CHECK256: call <vscale x 16 x i8> @llvm.aarch64.sve.asrd.nxv16i8(
// CHECK256: call <32 x i8> @llvm.vector.extract.v32i8.nxv16i8(
// CHECK256: store <32 x i8> {{.*}}, ptr {{.*}}, align 16
vec_int8 f2(vec_int8 x) { return svasrd_x(svptrue_b8(), x, 1); }
