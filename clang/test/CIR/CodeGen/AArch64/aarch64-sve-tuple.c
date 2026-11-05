// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the AArch64 SVE multi-vector tuple builtins
// (svcreate/svget/svset/svundef, the structured svld2/svst2, and svtbl2) to the
// same tuple insertvalue/extractvalue sequence and @llvm.aarch64.sve.*
// intrinsics as classic CodeGen. The SVE tuple is a struct of N scalable
// vectors in both compilers.
//
// Note: for functions whose *result* is a tuple, ClangIR spells the return
// struct as a packed literal (`<{...}>`) while classic uses an unpacked
// literal (`{...}`); this is a pre-existing ClangIR ABI-lowering property of any
// SVE-tuple-returning function (it reproduces without any builtin) and is
// orthogonal to the builtin lowering under test. The CHECK lines below match the
// intrinsic calls and tuple insert/extract ops, which are identical.

#include <arm_sve.h>

// svtbl2: consumes an x2 tuple (unpacked into its two component vectors) plus an
// index vector, calls llvm.aarch64.sve.tbl2. Result is a single vector, so the
// body byte-matches classic.
svint8_t test_tbl2_s8(svint8x2_t data, svuint8_t indices) {
  return svtbl2_s8(data, indices);
}
// CIR-LABEL: @test_tbl2_s8
// CIR: %[[V0:.*]] = extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 0
// CIR: %[[V1:.*]] = extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 1
// CIR: call <vscale x 16 x i8> @llvm.aarch64.sve.tbl2.nxv16i8(<vscale x 16 x i8> %[[V0]], <vscale x 16 x i8> %[[V1]], <vscale x 16 x i8> %{{.*}})
// OGCG-LABEL: @test_tbl2_s8
// OGCG: call <vscale x 16 x i8> @llvm.aarch64.sve.tbl2.nxv16i8(

// svcreate2: assemble the two component vectors into a poison tuple via a chain
// of insertvalue.
svint8x2_t test_create2_s8(svint8_t a, svint8_t b) {
  return svcreate2_s8(a, b);
}
// CIR-LABEL: @test_create2_s8
// CIR: %[[T0:.*]] = insertvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } {{poison|undef}}, <vscale x 16 x i8> %{{.*}}, 0
// CIR: insertvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %[[T0]], <vscale x 16 x i8> %{{.*}}, 1
// OGCG-LABEL: @test_create2_s8
// OGCG: %[[T0:.*]] = insertvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } poison, <vscale x 16 x i8> %{{.*}}, 0
// OGCG: insertvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %[[T0]], <vscale x 16 x i8> %{{.*}}, 1

// svget2 index 0 and 1: extractvalue of the requested tuple member.
svint8_t test_get2_s8_0(svint8x2_t t) { return svget2_s8(t, 0); }
// CIR-LABEL: @test_get2_s8_0
// CIR: extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 0
// OGCG-LABEL: @test_get2_s8_0
// OGCG: extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 0

svint8_t test_get2_s8_1(svint8x2_t t) { return svget2_s8(t, 1); }
// CIR-LABEL: @test_get2_s8_1
// CIR: extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 1
// OGCG-LABEL: @test_get2_s8_1
// OGCG: extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 1

// svset2: insertvalue into the requested tuple member.
svint8x2_t test_set2_s8(svint8x2_t t, svint8_t v) { return svset2_s8(t, 1, v); }
// CIR-LABEL: @test_set2_s8
// CIR: insertvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, <vscale x 16 x i8> %{{.*}}, 1
// OGCG-LABEL: @test_set2_s8
// OGCG: insertvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, <vscale x 16 x i8> %{{.*}}, 1

// svundef2: an undef/poison tuple. At -O0 both compilers scaffold the return
// value through the (u)ndef aggregate; the CIR tuple is spelled with the packed
// return struct.
svint8x2_t test_undef2_s8(void) { return svundef2_s8(); }
// CIR-LABEL: @test_undef2_s8
// CIR: {{(insertvalue|extractvalue|ret)}} {{.*}}<vscale x 16 x i8>{{.*}} undef
// OGCG-LABEL: @test_undef2_s8
// OGCG: ret { <vscale x 16 x i8>, <vscale x 16 x i8> } undef

// svld2: predicate narrowed to the data lane count, ld2_sret returns the tuple.
svint8x2_t test_ld2_s8(svbool_t pg, const signed char *base) {
  return svld2_s8(pg, base);
}
// CIR-LABEL: @test_ld2_s8
// CIR: call { <vscale x 16 x i8>, <vscale x 16 x i8> } @llvm.aarch64.sve.ld2.sret.nxv16i8(<vscale x 16 x i1> %{{.*}}, ptr %{{.*}})
// OGCG-LABEL: @test_ld2_s8
// OGCG: call { <vscale x 16 x i8>, <vscale x 16 x i8> } @llvm.aarch64.sve.ld2.sret.nxv16i8(<vscale x 16 x i1> %{{.*}}, ptr %{{.*}})

// svld2_vnum: base pointer strided by vnum data vectors before ld2_sret.
svint8x2_t test_ld2_vnum_s8(svbool_t pg, const signed char *base, long vnum) {
  return svld2_vnum_s8(pg, base, vnum);
}
// CIR-LABEL: @test_ld2_vnum_s8
// CIR: getelementptr <vscale x 16 x i8>, ptr %{{.*}}, i64 %{{.*}}
// CIR: call { <vscale x 16 x i8>, <vscale x 16 x i8> } @llvm.aarch64.sve.ld2.sret.nxv16i8(
// OGCG-LABEL: @test_ld2_vnum_s8
// OGCG: getelementptr <vscale x 16 x i8>, ptr %{{.*}}, i64 %{{.*}}
// OGCG: call { <vscale x 16 x i8>, <vscale x 16 x i8> } @llvm.aarch64.sve.ld2.sret.nxv16i8(

// svst2: tuple unpacked into components, then st2 with (vec0, vec1, pred, ptr).
void test_st2_s8(svbool_t pg, signed char *base, svint8x2_t data) {
  svst2_s8(pg, base, data);
}
// CIR-LABEL: @test_st2_s8
// CIR: call void @llvm.aarch64.sve.st2.nxv16i8(<vscale x 16 x i8> %{{.*}}, <vscale x 16 x i8> %{{.*}}, <vscale x 16 x i1> %{{.*}}, ptr %{{.*}})
// OGCG-LABEL: @test_st2_s8
// OGCG: call void @llvm.aarch64.sve.st2.nxv16i8(<vscale x 16 x i8> %{{.*}}, <vscale x 16 x i8> %{{.*}}, <vscale x 16 x i1> %{{.*}}, ptr %{{.*}})

// svst2_vnum: base pointer strided by vnum data vectors before st2.
void test_st2_vnum_s8(svbool_t pg, signed char *base, long vnum,
                      svint8x2_t data) {
  svst2_vnum_s8(pg, base, vnum, data);
}
// CIR-LABEL: @test_st2_vnum_s8
// CIR: getelementptr <vscale x 16 x i8>, ptr %{{.*}}, i64 %{{.*}}
// CIR: call void @llvm.aarch64.sve.st2.nxv16i8(
// OGCG-LABEL: @test_st2_vnum_s8
// OGCG: getelementptr <vscale x 16 x i8>, ptr %{{.*}}, i64 %{{.*}}
// OGCG: call void @llvm.aarch64.sve.st2.nxv16i8(

// Type coverage: s32 create/get, f32 create/get, f64 create/get.
svint32x2_t test_create2_s32(svint32_t a, svint32_t b) {
  return svcreate2_s32(a, b);
}
// CIR-LABEL: @test_create2_s32
// CIR: insertvalue { <vscale x 4 x i32>, <vscale x 4 x i32> } {{poison|undef}}, <vscale x 4 x i32> %{{.*}}, 0
// OGCG-LABEL: @test_create2_s32
// OGCG: insertvalue { <vscale x 4 x i32>, <vscale x 4 x i32> } poison, <vscale x 4 x i32> %{{.*}}, 0

svfloat32_t test_get2_f32(svfloat32x2_t t) { return svget2_f32(t, 0); }
// CIR-LABEL: @test_get2_f32
// CIR: extractvalue { <vscale x 4 x float>, <vscale x 4 x float> } %{{.*}}, 0
// OGCG-LABEL: @test_get2_f32
// OGCG: extractvalue { <vscale x 4 x float>, <vscale x 4 x float> } %{{.*}}, 0

svfloat64_t test_get2_f64(svfloat64x2_t t) { return svget2_f64(t, 1); }
// CIR-LABEL: @test_get2_f64
// CIR: extractvalue { <vscale x 2 x double>, <vscale x 2 x double> } %{{.*}}, 1
// OGCG-LABEL: @test_get2_f64
// OGCG: extractvalue { <vscale x 2 x double>, <vscale x 2 x double> } %{{.*}}, 1
