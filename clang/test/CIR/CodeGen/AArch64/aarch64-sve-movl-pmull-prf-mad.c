// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers four previously-NYI SVE builtin families to the
// same @llvm.aarch64.sve.* intrinsics as classic CodeGen: the svmovl widening
// moves (shift-left-long by immediate 0), the svpmull widening polynomial
// multiply-long (pair intrinsic + bitcast), the contiguous svprf prefetch
// family (convert.from.svbool predicate narrowing + prf), and the reversed-
// operand svmad/svmsb/svnmad/svnmsb FMA (fmla_u/fmls_u/... with the
// multiplicand and addend swapped).

#include <arm_sve.h>

// svmovlb_u32: unsigned widening move -> ushllb with a zero shift.
svuint32_t test_movlb_u32(svuint16_t a) { return svmovlb_u32(a); }
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_movlb_u32
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.ushllb.nxv4i32(<vscale x 8 x i16> %{{.*}}, i32 0)
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_movlb_u32
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.ushllb.nxv4i32(<vscale x 8 x i16> %{{.*}}, i32 0)

// svmovlt_s32: signed top widening move -> sshllt with a zero shift.
svint32_t test_movlt_s32(svint16_t a) { return svmovlt_s32(a); }
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_movlt_s32
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.sshllt.nxv4i32(<vscale x 8 x i16> %{{.*}}, i32 0)
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_movlt_s32
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.sshllt.nxv4i32(<vscale x 8 x i16> %{{.*}}, i32 0)

// svmovlb_s16: 8->16 signed widening move -> sshllb.nxv8i16.
svint16_t test_movlb_s16(svint8_t a) { return svmovlb_s16(a); }
// CIR-LABEL: define{{.*}} <vscale x 8 x i16> @test_movlb_s16
// CIR: call <vscale x 8 x i16> @llvm.aarch64.sve.sshllb.nxv8i16(<vscale x 16 x i8> %{{.*}}, i32 0)
// OGCG-LABEL: define{{.*}} <vscale x 8 x i16> @test_movlb_s16
// OGCG: call <vscale x 8 x i16> @llvm.aarch64.sve.sshllb.nxv8i16(<vscale x 16 x i8> %{{.*}}, i32 0)

// svpmullb_u16: 8->16 widening polymul -> pmullb.pair.nxv16i8 then bitcast to
// <vscale x 8 x i16>.
svuint16_t test_pmullb_u16(svuint8_t a, svuint8_t b) { return svpmullb_u16(a, b); }
// CIR-LABEL: define{{.*}} <vscale x 8 x i16> @test_pmullb_u16
// CIR: %[[C:.*]] = call <vscale x 16 x i8> @llvm.aarch64.sve.pmullb.pair.nxv16i8(<vscale x 16 x i8> %{{.*}}, <vscale x 16 x i8> %{{.*}})
// CIR: bitcast <vscale x 16 x i8> %[[C]] to <vscale x 8 x i16>
// OGCG-LABEL: define{{.*}} <vscale x 8 x i16> @test_pmullb_u16
// OGCG: %[[C:.*]] = call <vscale x 16 x i8> @llvm.aarch64.sve.pmullb.pair.nxv16i8(<vscale x 16 x i8> %{{.*}}, <vscale x 16 x i8> %{{.*}})
// OGCG: bitcast <vscale x 16 x i8> %[[C]] to <vscale x 8 x i16>

// svpmullt_u16: 8->16 top widening polymul -> pmullt.pair.
svuint16_t test_pmullt_u16(svuint8_t a, svuint8_t b) { return svpmullt_u16(a, b); }
// CIR-LABEL: define{{.*}} <vscale x 8 x i16> @test_pmullt_u16
// CIR: call <vscale x 16 x i8> @llvm.aarch64.sve.pmullt.pair.nxv16i8
// OGCG-LABEL: define{{.*}} <vscale x 8 x i16> @test_pmullt_u16
// OGCG: call <vscale x 16 x i8> @llvm.aarch64.sve.pmullt.pair.nxv16i8

// svpmullb_n_u16: _n form splats the scalar operand before the pair intrinsic.
svuint16_t test_pmullb_n_u16(svuint8_t a, uint8_t b) { return svpmullb_n_u16(a, b); }
// CIR-LABEL: define{{.*}} <vscale x 8 x i16> @test_pmullb_n_u16
// CIR: %[[I:.*]] = insertelement <vscale x 16 x i8> poison, i8 %{{.*}}, i64 0
// CIR: %[[S:.*]] = shufflevector <vscale x 16 x i8> %[[I]], <vscale x 16 x i8> poison, <vscale x 16 x i32> zeroinitializer
// CIR: call <vscale x 16 x i8> @llvm.aarch64.sve.pmullb.pair.nxv16i8(<vscale x 16 x i8> %{{.*}}, <vscale x 16 x i8> %[[S]])
// OGCG-LABEL: define{{.*}} <vscale x 8 x i16> @test_pmullb_n_u16
// OGCG: call <vscale x 16 x i8> @llvm.aarch64.sve.pmullb.pair.nxv16i8

// svprfb: byte prefetch, no predicate narrowing (nxv16i1), prfop immediate 0.
void test_prfb(svbool_t pg, const void *base) { svprfb(pg, base, SV_PLDL1KEEP); }
// CIR-LABEL: define{{.*}} void @test_prfb
// CIR: call void @llvm.aarch64.sve.prf.nxv16i1(<vscale x 16 x i1> %{{.*}}, ptr %{{.*}}, i32 0)
// OGCG-LABEL: define{{.*}} void @test_prfb
// OGCG: call void @llvm.aarch64.sve.prf.nxv16i1(<vscale x 16 x i1> %{{.*}}, ptr %{{.*}}, i32 0)

// svprfw: word prefetch narrows the predicate to nxv4i1.
void test_prfw(svbool_t pg, const void *base) { svprfw(pg, base, SV_PLDL1KEEP); }
// CIR-LABEL: define{{.*}} void @test_prfw
// CIR: %[[P:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1(<vscale x 16 x i1> %{{.*}})
// CIR: call void @llvm.aarch64.sve.prf.nxv4i1(<vscale x 4 x i1> %[[P]], ptr %{{.*}}, i32 0)
// OGCG-LABEL: define{{.*}} void @test_prfw
// OGCG: call void @llvm.aarch64.sve.prf.nxv4i1

// svprfd_vnum: doubleword prefetch with a vnum stride (GEP by data vector).
void test_prfd_vnum(svbool_t pg, const void *base, int64_t vnum) {
  svprfd_vnum(pg, base, vnum, SV_PLDL1KEEP);
}
// CIR-LABEL: define{{.*}} void @test_prfd_vnum
// CIR: call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR: getelementptr <vscale x 2 x i64>
// CIR: call void @llvm.aarch64.sve.prf.nxv2i1
// OGCG-LABEL: define{{.*}} void @test_prfd_vnum
// OGCG: getelementptr <vscale x 2 x i64>
// OGCG: call void @llvm.aarch64.sve.prf.nxv2i1

// svmad_f32_x: reversed-operand FMA folds to fmla.u with the multiplicand and
// addend swapped (operands become c, b, a).
svfloat32_t test_mad_f32_x(svbool_t pg, svfloat32_t a, svfloat32_t b, svfloat32_t c) {
  return svmad_f32_x(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_mad_f32_x
// CIR: %[[A:.*]] = load{{.*}}
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.fmla.u.nxv4f32(<vscale x 4 x i1> %{{.*}}, <vscale x 4 x float> %[[C:.*]], <vscale x 4 x float> %[[B:.*]], <vscale x 4 x float> %[[AA:.*]])
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_mad_f32_x
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.fmla.u.nxv4f32

// svmsb_f32_x: reversed FMA subtract -> fmls.u.
svfloat32_t test_msb_f32_x(svbool_t pg, svfloat32_t a, svfloat32_t b, svfloat32_t c) {
  return svmsb_f32_x(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_msb_f32_x
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.fmls.u.nxv4f32
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_msb_f32_x
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.fmls.u.nxv4f32

// svnmad_f16_x -> fnmla.u; svnmsb_f64_x -> fnmls.u.
svfloat16_t test_nmad_f16_x(svbool_t pg, svfloat16_t a, svfloat16_t b, svfloat16_t c) {
  return svnmad_f16_x(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 8 x half> @test_nmad_f16_x
// CIR: call <vscale x 8 x half> @llvm.aarch64.sve.fnmla.u.nxv8f16
// OGCG-LABEL: define{{.*}} <vscale x 8 x half> @test_nmad_f16_x
// OGCG: call <vscale x 8 x half> @llvm.aarch64.sve.fnmla.u.nxv8f16

svfloat64_t test_nmsb_f64_x(svbool_t pg, svfloat64_t a, svfloat64_t b, svfloat64_t c) {
  return svnmsb_f64_x(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 2 x double> @test_nmsb_f64_x
// CIR: call <vscale x 2 x double> @llvm.aarch64.sve.fnmls.u.nxv2f64
// OGCG-LABEL: define{{.*}} <vscale x 2 x double> @test_nmsb_f64_x
// OGCG: call <vscale x 2 x double> @llvm.aarch64.sve.fnmls.u.nxv2f64

// svmad_s32_x: integer reversed FMA -> mla.u.
svint32_t test_mad_s32_x(svbool_t pg, svint32_t a, svint32_t b, svint32_t c) {
  return svmad_s32_x(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_mad_s32_x
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.mla.u.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_mad_s32_x
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.mla.u.nxv4i32

// svmad_s32_m (merge): NO swap, uses the base mad intrinsic with a, b, c order.
svint32_t test_mad_s32_m(svbool_t pg, svint32_t a, svint32_t b, svint32_t c) {
  return svmad_s32_m(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_mad_s32_m
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.mad.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_mad_s32_m
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.mad.nxv4i32

// svmad_s32_z (zeroing): select w/ zero then the base mad intrinsic (no swap).
svint32_t test_mad_s32_z(svbool_t pg, svint32_t a, svint32_t b, svint32_t c) {
  return svmad_s32_z(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_mad_s32_z
// CIR: select <vscale x 4 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> zeroinitializer
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.mad.nxv4i32
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_mad_s32_z
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.mad.nxv4i32

// Regression: forward svmla_f32_x is NOT reversed -> fmla.u with a, b, c order
// (the multiplicands stay in their original positions).
svfloat32_t test_mla_f32_x(svbool_t pg, svfloat32_t a, svfloat32_t b, svfloat32_t c) {
  return svmla_f32_x(pg, a, b, c);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_mla_f32_x
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.fmla.u.nxv4f32(<vscale x 4 x i1> %{{.*}}, <vscale x 4 x float> %[[MA:.*]], <vscale x 4 x float> %[[MB:.*]], <vscale x 4 x float> %[[MC:.*]])
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_mla_f32_x
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.fmla.u.nxv4f32
