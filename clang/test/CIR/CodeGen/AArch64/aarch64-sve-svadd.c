// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

#include <arm_sve.h>

svint32_t f_add_m(svbool_t pg, svint32_t a, svint32_t b) {
  return svadd_s32_m(pg, a, b);
}

// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @f_add_m
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.add.nxv4i32

// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @f_add_m
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.add.nxv4i32
