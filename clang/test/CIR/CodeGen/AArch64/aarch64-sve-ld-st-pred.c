// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers SVE contiguous load/store, gather/scatter, and the
// _x/_z predicated builtins to the same @llvm.aarch64.sve.* intrinsics (and the
// same convert.to/from.svbool predicate casts) as classic CodeGen.

#include <arm_sve.h>

// Contiguous load.
svint32_t test_ld1_s32(svbool_t pg, const int32_t *base) {
  return svld1_s32(pg, base);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_ld1_s32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.ld1.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_ld1_s32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.ld1.nxv4i32

svfloat64_t test_ld1_f64(svbool_t pg, const float64_t *base) {
  return svld1_f64(pg, base);
}
// CIR-LABEL: define{{.*}} <vscale x 2 x double> @test_ld1_f64
// CIR: call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR: call <vscale x 2 x double> @llvm.aarch64.sve.ld1.nxv2f64
// OGCG-LABEL: define{{.*}} <vscale x 2 x double> @test_ld1_f64
// OGCG: call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG: call <vscale x 2 x double> @llvm.aarch64.sve.ld1.nxv2f64

// Contiguous load of a full-width type needs no predicate narrowing.
svuint8_t test_ld1_u8(svbool_t pg, const uint8_t *base) {
  return svld1_u8(pg, base);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i8> @test_ld1_u8
// CIR: call <vscale x 16 x i8> @llvm.aarch64.sve.ld1.nxv16i8
// OGCG-LABEL: define{{.*}} <vscale x 16 x i8> @test_ld1_u8
// OGCG: call <vscale x 16 x i8> @llvm.aarch64.sve.ld1.nxv16i8

// Widening load: narrow memory element then sign extend.
svint32_t test_ld1sb_s32(svbool_t pg, const int8_t *base) {
  return svld1sb_s32(pg, base);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_ld1sb_s32
// CIR: call <vscale x 4 x i8> @llvm.aarch64.sve.ld1.nxv4i8
// CIR: sext <vscale x 4 x i8> {{.*}} to <vscale x 4 x i32>
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_ld1sb_s32
// OGCG: call <vscale x 4 x i8> @llvm.aarch64.sve.ld1.nxv4i8
// OGCG: sext <vscale x 4 x i8> {{.*}} to <vscale x 4 x i32>

// Contiguous store.
void test_st1_s32(svbool_t pg, int32_t *base, svint32_t v) {
  svst1_s32(pg, base, v);
}
// CIR-LABEL: define{{.*}} void @test_st1_s32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call void @llvm.aarch64.sve.st1.nxv4i32
// OGCG-LABEL: define{{.*}} void @test_st1_s32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call void @llvm.aarch64.sve.st1.nxv4i32

// Narrowing store: truncate then store the narrow element.
void test_st1b_s32(svbool_t pg, int8_t *base, svint32_t v) {
  svst1b_s32(pg, base, v);
}
// CIR-LABEL: define{{.*}} void @test_st1b_s32
// CIR: trunc <vscale x 4 x i32> {{.*}} to <vscale x 4 x i8>
// CIR: call void @llvm.aarch64.sve.st1.nxv4i8
// OGCG-LABEL: define{{.*}} void @test_st1b_s32
// OGCG: trunc <vscale x 4 x i32> {{.*}} to <vscale x 4 x i8>
// OGCG: call void @llvm.aarch64.sve.st1.nxv4i8

// Gather load, signed 32-bit index.
svint32_t test_gather_s32index(svbool_t pg, const int32_t *base, svint32_t idx) {
  return svld1_gather_s32index_s32(pg, base, idx);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_gather_s32index
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.ld1.gather.sxtw.index.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_gather_s32index
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.ld1.gather.sxtw.index.nxv4i32

// Gather load, signed 64-bit index (no extend of the offset).
svint64_t test_gather_s64index(svbool_t pg, const int64_t *base, svint64_t idx) {
  return svld1_gather_s64index_s64(pg, base, idx);
}
// CIR-LABEL: define{{.*}} <vscale x 2 x i64> @test_gather_s64index
// CIR: call <vscale x 2 x i64> @llvm.aarch64.sve.ld1.gather.index.nxv2i64
// OGCG-LABEL: define{{.*}} <vscale x 2 x i64> @test_gather_s64index
// OGCG: call <vscale x 2 x i64> @llvm.aarch64.sve.ld1.gather.index.nxv2i64

// Scatter store, signed 32-bit index.
void test_scatter_s32index(svbool_t pg, int32_t *base, svint32_t idx,
                           svint32_t v) {
  svst1_scatter_s32index_s32(pg, base, idx, v);
}
// CIR-LABEL: define{{.*}} void @test_scatter_s32index
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call void @llvm.aarch64.sve.st1.scatter.sxtw.index.nxv4i32
// OGCG-LABEL: define{{.*}} void @test_scatter_s32index
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call void @llvm.aarch64.sve.st1.scatter.sxtw.index.nxv4i32

// _x predicated unary with passthrough: undef inactive operand.
svint32_t test_abs_x(svbool_t pg, svint32_t a) {
  return svabs_s32_x(pg, a);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_abs_x
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.abs.nxv4i32(<vscale x 4 x i32> undef, <vscale x 4 x i1> {{.*}}, <vscale x 4 x i32> {{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_abs_x
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.abs.nxv4i32(<vscale x 4 x i32> undef, <vscale x 4 x i1> {{.*}}, <vscale x 4 x i32> {{.*}})

// _x convert with passthrough.
svfloat32_t test_cvt_x(svbool_t pg, svint32_t a) {
  return svcvt_f32_s32_x(pg, a);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_cvt_x
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.scvtf.nxv4f32.nxv4i32(<vscale x 4 x float> undef, <vscale x 4 x i1> {{.*}}, <vscale x 4 x i32> {{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_cvt_x
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.scvtf.nxv4f32.nxv4i32(<vscale x 4 x float> undef, <vscale x 4 x i1> {{.*}}, <vscale x 4 x i32> {{.*}})

// _x binary: uses the undef-merge ".u" intrinsic, no explicit passthrough.
svint32_t test_add_x(svbool_t pg, svint32_t a, svint32_t b) {
  return svadd_s32_x(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_add_x
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.add.u.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_add_x
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.add.u.nxv4i32

// _z predicated unary with passthrough: zeroinitializer inactive operand.
svint32_t test_abs_z(svbool_t pg, svint32_t a) {
  return svabs_s32_z(pg, a);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_abs_z
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.abs.nxv4i32(<vscale x 4 x i32> zeroinitializer, <vscale x 4 x i1> {{.*}}, <vscale x 4 x i32> {{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_abs_z
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.abs.nxv4i32(<vscale x 4 x i32> zeroinitializer, <vscale x 4 x i1> {{.*}}, <vscale x 4 x i32> {{.*}})

// _z binary: select the first data operand with a zero splat before the op.
svint32_t test_add_z(svbool_t pg, svint32_t a, svint32_t b) {
  return svadd_s32_z(pg, a, b);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_add_z
// CIR: %[[PZ:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: select <vscale x 4 x i1> %[[PZ]], <vscale x 4 x i32> {{.*}}, <vscale x 4 x i32> zeroinitializer
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.add.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_add_z
// OGCG: %[[PZ2:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: select <vscale x 4 x i1> %[[PZ2]], <vscale x 4 x i32> {{.*}}, <vscale x 4 x i32> zeroinitializer
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.add.nxv4i32
