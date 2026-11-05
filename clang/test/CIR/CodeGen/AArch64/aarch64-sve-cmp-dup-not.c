// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the SVE reversed-operand compares (svcmplt/svcmple/
// svaclt/svacle -> the forward svcmpgt/svcmpge/svacgt/svacge intrinsic with the
// two data operands swapped), svdup_lane (a lane-index splat feeding the tbl
// intrinsic), and svnot_b_z (eor.z against the governing predicate) to the same
// @llvm.aarch64.sve.* intrinsics as classic CodeGen.

#include <arm_sve.h>

// Reversed integer compare: svcmplt lowers to cmpgt with the data operands
// swapped (b before a).
svbool_t test_cmplt_s32(svbool_t pg, svint32_t a, svint32_t b) {
  return svcmplt_s32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_s32
// CIR: %[[P:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1(<vscale x 16 x i1> %{{.*}})
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpgt.nxv4i32(<vscale x 4 x i1> %[[P]], <vscale x 4 x i32> %[[B:.*]], <vscale x 4 x i32> %[[A:.*]])
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_s32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpgt.nxv4i32

// Reversed unsigned compare: svcmplt_u32 -> cmphi swapped.
svbool_t test_cmplt_u32(svbool_t pg, svuint32_t a, svuint32_t b) {
  return svcmplt_u32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_u32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.cmphi.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_u32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.cmphi.nxv4i32

// Reversed float compare: svcmplt_f32 -> fcmpgt swapped.
svbool_t test_cmplt_f32(svbool_t pg, svfloat32_t a, svfloat32_t b) {
  return svcmplt_f32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_f32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.fcmpgt.nxv4f32
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_f32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.fcmpgt.nxv4f32

// Reversed less-equal: svcmple_s32 -> cmpge swapped.
svbool_t test_cmple_s32(svbool_t pg, svint32_t a, svint32_t b) {
  return svcmple_s32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmple_s32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpge.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmple_s32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpge.nxv4i32

// Reversed absolute compares: svaclt/svacle -> facgt/facge swapped.
svbool_t test_aclt_f32(svbool_t pg, svfloat32_t a, svfloat32_t b) {
  return svaclt_f32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_aclt_f32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.facgt.nxv4f32
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_aclt_f32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.facgt.nxv4f32

svbool_t test_acle_f32(svbool_t pg, svfloat32_t a, svfloat32_t b) {
  return svacle_f32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_acle_f32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.facge.nxv4f32
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_acle_f32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.facge.nxv4f32

// Byte-element reversed compare stays at nxv16i1 (no predicate cast).
svbool_t test_cmplt_s8(svbool_t pg, svint8_t a, svint8_t b) {
  return svcmplt_s8(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_s8
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.cmpgt.nxv16i8
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmplt_s8
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.cmpgt.nxv16i8

// Forward compare (regression): svcmpgt is unchanged.
svbool_t test_cmpgt_s32(svbool_t pg, svint32_t a, svint32_t b) {
  return svcmpgt_s32(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmpgt_s32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpgt.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_cmpgt_s32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpgt.nxv4i32

// svdup_lane: trunc the lane index to the data element width, splat it, and
// call tbl. For s32 the index is splatted to <vscale x 4 x i32>.
svint32_t test_dup_lane_s32(svint32_t a, uint64_t idx) {
  return svdup_lane_s32(a, idx);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_dup_lane_s32
// CIR: %[[T:.*]] = trunc i64 %{{.*}} to i32
// CIR: %[[I:.*]] = insertelement <vscale x 4 x i32> poison, i32 %[[T]], i64 0
// CIR: %[[S:.*]] = shufflevector <vscale x 4 x i32> %[[I]], <vscale x 4 x i32> poison, <vscale x 4 x i32> zeroinitializer
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.tbl.nxv4i32(<vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %[[S]])
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_dup_lane_s32
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.tbl.nxv4i32

// svdup_lane for a float type: the index vector is still integer (nxv4i32),
// the tbl is overloaded on the float data type.
svfloat32_t test_dup_lane_f32(svfloat32_t a, uint64_t idx) {
  return svdup_lane_f32(a, idx);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_dup_lane_f32
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.tbl.nxv4f32(<vscale x 4 x float> %{{.*}}, <vscale x 4 x i32> %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_dup_lane_f32
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.tbl.nxv4f32

// svdup_lane_f64: index is i64, no trunc, splatted to <vscale x 2 x i64>.
svfloat64_t test_dup_lane_f64(svfloat64_t a, uint64_t idx) {
  return svdup_lane_f64(a, idx);
}
// CIR-LABEL: define{{.*}} <vscale x 2 x double> @test_dup_lane_f64
// CIR: insertelement <vscale x 2 x i64> poison, i64 %{{.*}}, i64 0
// CIR: call <vscale x 2 x double> @llvm.aarch64.sve.tbl.nxv2f64(<vscale x 2 x double> %{{.*}}, <vscale x 2 x i64> %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 2 x double> @test_dup_lane_f64
// OGCG: call <vscale x 2 x double> @llvm.aarch64.sve.tbl.nxv2f64

// svnot_b_z: eor.z(pg, a, pg).
svbool_t test_not_b_z(svbool_t pg, svbool_t a) {
  return svnot_b_z(pg, a);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_not_b_z
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.eor.z.nxv16i1(<vscale x 16 x i1> %[[PG:.*]], <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %[[PG]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_not_b_z
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.eor.z.nxv16i1

// Sibling predicate-logic op (regression): sveor_b_z is unchanged.
svbool_t test_eor_b_z(svbool_t pg, svbool_t a, svbool_t b) {
  return sveor_b_z(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_eor_b_z
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.eor.z.nxv16i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_eor_b_z
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.eor.z.nxv16i1
