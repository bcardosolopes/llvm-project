// REQUIRES: aarch64-registered-target
//
// Floating-point `_n_` (scalar-broadcast) SVE intrinsics splat their scalar
// operand to a scalable vector before feeding the aarch64.sve.* intrinsic.
// getSVEVectorForElementType(mlir::Type) used to cast the element type to
// cir::IntType unconditionally, so a float scalar (f32/f64) crashed CIRGen.
// It now derives the lane count from the element bit width for both integer
// and floating-point elements, matching classic codegen.
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

#include <arm_sve.h>

svfloat32_t f_add_n_f32(svbool_t pg, svfloat32_t v) {
  return svadd_n_f32_m(pg, v, 1.0f);
}

// CIR-LABEL: define{{.*}} <vscale x 4 x float> @f_add_n_f32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.fadd.nxv4f32(<vscale x 4 x i1> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> splat (float 1.000000e+00))

// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @f_add_n_f32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.fadd.nxv4f32(<vscale x 4 x i1> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> splat (float 1.000000e+00))

svfloat64_t f_add_n_f64(svbool_t pg, svfloat64_t v) {
  return svadd_n_f64_m(pg, v, 1.0);
}

// CIR-LABEL: define{{.*}} <vscale x 2 x double> @f_add_n_f64
// CIR: call <vscale x 2 x double> @llvm.aarch64.sve.fadd.nxv2f64(<vscale x 2 x i1> %{{.*}}, <vscale x 2 x double> %{{.*}}, <vscale x 2 x double> splat (double 1.000000e+00))

// OGCG-LABEL: define{{.*}} <vscale x 2 x double> @f_add_n_f64
// OGCG: call <vscale x 2 x double> @llvm.aarch64.sve.fadd.nxv2f64(<vscale x 2 x i1> %{{.*}}, <vscale x 2 x double> %{{.*}}, <vscale x 2 x double> splat (double 1.000000e+00))
