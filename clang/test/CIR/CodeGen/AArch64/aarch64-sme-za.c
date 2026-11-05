// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the AArch64 SME ZA-storage and outer-product
// builtins to the same @llvm.aarch64.sme.* intrinsics (and the same
// convert.from.svbool predicate casts) as classic CodeGen. Tested at the raw
// __builtin_sme_* level to avoid the generated arm_sme.h header dependency.

typedef __SVInt8_t svint8_t;
typedef __SVInt32_t svint32_t;
typedef __SVUint32_t svuint32_t;
typedef __SVFloat32_t svfloat32_t;
typedef __SVBool_t svbool_t;

// Outer-product accumulate, s8 widening form: predicates already match the i8
// (nxv16i1) lane count, so no convert.from.svbool is emitted.
void test_mopa_s8(svbool_t pn, svbool_t pm, svint8_t a, svint8_t b)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svmopa_za32_s8_m(0, pn, pm, a, b);
}
// CIR-LABEL: @test_mopa_s8
// CIR: call void @llvm.aarch64.sme.smopa.wide.nxv16i8(i32 0,
// OGCG-LABEL: @test_mopa_s8
// OGCG: call void @llvm.aarch64.sme.smopa.wide.nxv16i8(i32 0,

// Outer-product subtract, s8 widening form.
void test_mops_s8(svbool_t pn, svbool_t pm, svint8_t a, svint8_t b)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svmops_za32_s8_m(0, pn, pm, a, b);
}
// CIR-LABEL: @test_mops_s8
// CIR: call void @llvm.aarch64.sme.smops.wide.nxv16i8(i32 0,
// OGCG-LABEL: @test_mops_s8
// OGCG: call void @llvm.aarch64.sme.smops.wide.nxv16i8(i32 0,

// Outer-product accumulate, f32 form: predicates are narrowed to nxv4i1.
void test_mopa_f32(svbool_t pn, svbool_t pm, svfloat32_t a, svfloat32_t b)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svmopa_za32_f32_m(0, pn, pm, a, b);
}
// CIR-LABEL: @test_mopa_f32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call void @llvm.aarch64.sme.mopa.nxv4f32(i32 0,
// OGCG-LABEL: @test_mopa_f32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call void @llvm.aarch64.sme.mopa.nxv4f32(i32 0,

// ZA add-horizontal.
void test_addha(svbool_t pn, svbool_t pm, svuint32_t a)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svaddha_za32_u32_m(0, pn, pm, a);
}
// CIR-LABEL: @test_addha
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call void @llvm.aarch64.sme.addha.nxv4i32(i32 0,
// OGCG-LABEL: @test_addha
// OGCG: call void @llvm.aarch64.sme.addha.nxv4i32(i32 0,

// ZA add-vertical.
void test_addva(svbool_t pn, svbool_t pm, svuint32_t a)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svaddva_za32_u32_m(0, pn, pm, a);
}
// CIR-LABEL: @test_addva
// CIR: call void @llvm.aarch64.sme.addva.nxv4i32(i32 0,
// OGCG-LABEL: @test_addva
// OGCG: call void @llvm.aarch64.sme.addva.nxv4i32(i32 0,

// ZA tile read, horizontal.
svint32_t test_read_hor(svint32_t zd, svbool_t pg, unsigned slice)
    __arm_streaming __arm_in("za") {
  return __builtin_sme_svread_hor_za32_s32_m(zd, pg, 0, slice);
}
// CIR-LABEL: @test_read_hor
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sme.read.horiz.nxv4i32(<vscale x 4 x i32> {{.*}}, <vscale x 4 x i1> {{.*}}, i32 0, i32
// OGCG-LABEL: @test_read_hor
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sme.read.horiz.nxv4i32(<vscale x 4 x i32> {{.*}}, <vscale x 4 x i1> {{.*}}, i32 0, i32

// ZA tile read, vertical.
svint32_t test_read_ver(svint32_t zd, svbool_t pg, unsigned slice)
    __arm_streaming __arm_in("za") {
  return __builtin_sme_svread_ver_za32_s32_m(zd, pg, 0, slice);
}
// CIR-LABEL: @test_read_ver
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sme.read.vert.nxv4i32(
// OGCG-LABEL: @test_read_ver
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sme.read.vert.nxv4i32(

// ZA tile write, horizontal.
void test_write_hor(svbool_t pg, unsigned slice, svint32_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svwrite_hor_za32_s32_m(0, slice, pg, zn);
}
// CIR-LABEL: @test_write_hor
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call void @llvm.aarch64.sme.write.horiz.nxv4i32(i32 0, i32 {{.*}}, <vscale x 4 x i1>
// OGCG-LABEL: @test_write_hor
// OGCG: call void @llvm.aarch64.sme.write.horiz.nxv4i32(i32 0, i32 {{.*}}, <vscale x 4 x i1>

// ZA tile write, vertical.
void test_write_ver(svbool_t pg, unsigned slice, svint32_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svwrite_ver_za32_s32_m(0, slice, pg, zn);
}
// CIR-LABEL: @test_write_ver
// CIR: call void @llvm.aarch64.sme.write.vert.nxv4i32(i32 0, i32
// OGCG-LABEL: @test_write_ver
// OGCG: call void @llvm.aarch64.sme.write.vert.nxv4i32(i32 0, i32

// ZA tile load, horizontal (no vnum): (pred, ptr, tile#, slice) operand order.
void test_ld1_hor(svbool_t pg, const void *ptr, unsigned slice)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svld1_hor_za32(0, slice, pg, ptr);
}
// CIR-LABEL: @test_ld1_hor
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR: call void @llvm.aarch64.sme.ld1w.horiz(<vscale x 4 x i1> {{.*}}, ptr {{.*}}, i32 0, i32
// OGCG-LABEL: @test_ld1_hor
// OGCG: call void @llvm.aarch64.sme.ld1w.horiz(<vscale x 4 x i1> {{.*}}, ptr {{.*}}, i32 0, i32

// ZA tile load, horizontal with vnum: byte-GEP by vnum*svl and slice += vnum.
void test_ld1_hor_vnum(svbool_t pg, const void *ptr, unsigned slice, long vnum)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svld1_hor_vnum_za32(0, slice, pg, ptr, vnum);
}
// CIR-LABEL: @test_ld1_hor_vnum
// CIR: call i64 @llvm.aarch64.sme.cntsd()
// CIR: mul nuw nsw i64
// CIR: getelementptr i8, ptr
// CIR: call void @llvm.aarch64.sme.ld1w.horiz(
// OGCG-LABEL: @test_ld1_hor_vnum
// OGCG: call i64 @llvm.aarch64.sme.cntsd()
// OGCG: mul nuw nsw i64
// OGCG: getelementptr i8, ptr
// OGCG: call void @llvm.aarch64.sme.ld1w.horiz(

// ZA tile store, vertical.
void test_st1_ver(svbool_t pg, void *ptr, unsigned slice)
    __arm_streaming __arm_in("za") {
  __builtin_sme_svst1_ver_za32(0, slice, pg, ptr);
}
// CIR-LABEL: @test_st1_ver
// CIR: call void @llvm.aarch64.sme.st1w.vert(<vscale x 4 x i1> {{.*}}, ptr {{.*}}, i32 0, i32
// OGCG-LABEL: @test_st1_ver
// OGCG: call void @llvm.aarch64.sme.st1w.vert(<vscale x 4 x i1> {{.*}}, ptr {{.*}}, i32 0, i32

// Zero ZA (masked): the mask immediate flows straight through.
void test_zero_mask(void) __arm_streaming __arm_out("za") {
  __builtin_sme_svzero_mask_za(0);
}
// CIR-LABEL: @test_zero_mask
// CIR: call void @llvm.aarch64.sme.zero(i32 0)
// OGCG-LABEL: @test_zero_mask
// OGCG: call void @llvm.aarch64.sme.zero(i32 0)
