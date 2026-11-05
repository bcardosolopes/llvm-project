// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -target-feature +sve2p1 -target-feature +sme2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -target-feature +sve2p1 -target-feature +sme2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the remaining SVE predicate-family builtins to the
// same @llvm.aarch64.sve.* sequence as classic CodeGen:
//   - svmov_b_z(pg, op)   -> and.z(pg, op, op)  (zeroing predicate MOV, the
//     sibling of svnot_b_z's eor.z(pg, op, pg)).
//   - svpsel_lane_{b,c}*  -> psel overloaded on the narrowed second predicate;
//     the _c* forms round-trip the svcount_t predicate pair through
//     convert.to/from.svbool on the svcount target-ext type.
//   - svreinterpret_b/c   -> convert.to/from.svbool overloaded on the svcount
//     target-ext type (a plain svbool_t <-> svcount_t reinterpret).

#include <arm_sve.h>

// svmov_b_z: AND the predicate against itself under the governing predicate.
svbool_t test_mov_b_z(svbool_t pg, svbool_t op) {
  return svmov_b_z(pg, op);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_mov_b_z
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.and.z.nxv16i1(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %[[MOP:.*]], <vscale x 16 x i1> %[[MOP]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_mov_b_z
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.and.z.nxv16i1(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %[[OMOP:.*]], <vscale x 16 x i1> %[[OMOP]])

// svpsel_lane_b8: the second predicate is already nxv16i1, so no narrowing.
svbool_t test_psel_b8(svbool_t pg, svbool_t op, uint32_t i) {
  return svpsel_lane_b8(pg, op, i);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b8
// CIR-NOT: convert.from.svbool
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv16i1(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, i32 %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b8
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv16i1

// svpsel_lane_b16: narrow the second predicate to nxv8i1 before psel.
svbool_t test_psel_b16(svbool_t pg, svbool_t op, uint32_t i) {
  return svpsel_lane_b16(pg, op, i);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b16
// CIR: call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1(<vscale x 16 x i1> %{{.*}})
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv8i1(<vscale x 16 x i1> %{{.*}}, <vscale x 8 x i1> %{{.*}}, i32 %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b16
// OGCG: call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv8i1

// svpsel_lane_b32: narrow to nxv4i1.
svbool_t test_psel_b32(svbool_t pg, svbool_t op, uint32_t i) {
  return svpsel_lane_b32(pg, op, i);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1(<vscale x 16 x i1> %{{.*}})
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv4i1(<vscale x 16 x i1> %{{.*}}, <vscale x 4 x i1> %{{.*}}, i32 %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv4i1

// svpsel_lane_b64: narrow to nxv2i1.
svbool_t test_psel_b64(svbool_t pg, svbool_t op, uint32_t i) {
  return svpsel_lane_b64(pg, op, i);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b64
// CIR: call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %{{.*}})
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv2i1(<vscale x 16 x i1> %{{.*}}, <vscale x 2 x i1> %{{.*}}, i32 %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_psel_b64
// OGCG: call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv2i1

// svpsel_lane_c8: svcount_t pair in/out; op already nxv16i1 (no narrowing).
svcount_t test_psel_c8(svcount_t c, svbool_t op, uint32_t i) {
  return svpsel_lane_c8(c, op, i);
}
// CIR-LABEL: define{{.*}} target("aarch64.svcount") @test_psel_c8
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.taarch64.svcountt(target("aarch64.svcount") %{{.*}})
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv16i1(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, i32 %{{.*}})
// CIR: call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt(<vscale x 16 x i1> %{{.*}})
// OGCG-LABEL: define{{.*}} target("aarch64.svcount") @test_psel_c8
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.taarch64.svcountt
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv16i1
// OGCG: call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt

// svpsel_lane_c32: svcount_t pair in/out; op narrowed to nxv4i1.
svcount_t test_psel_c32(svcount_t c, svbool_t op, uint32_t i) {
  return svpsel_lane_c32(c, op, i);
}
// CIR-LABEL: define{{.*}} target("aarch64.svcount") @test_psel_c32
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.taarch64.svcountt(target("aarch64.svcount") %{{.*}})
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1(<vscale x 16 x i1> %{{.*}})
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv4i1(<vscale x 16 x i1> %{{.*}}, <vscale x 4 x i1> %{{.*}}, i32 %{{.*}})
// CIR: call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt(<vscale x 16 x i1> %{{.*}})
// OGCG-LABEL: define{{.*}} target("aarch64.svcount") @test_psel_c32
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.taarch64.svcountt
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.psel.nxv4i1
// OGCG: call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt

// svreinterpret_b: svcount_t -> svbool_t.
svbool_t test_reinterpret_b(svcount_t c) {
  return svreinterpret_b(c);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_reinterpret_b
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.taarch64.svcountt(target("aarch64.svcount") %{{.*}})
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_reinterpret_b
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.taarch64.svcountt

// svreinterpret_c: svbool_t -> svcount_t.
svcount_t test_reinterpret_c(svbool_t b) {
  return svreinterpret_c(b);
}
// CIR-LABEL: define{{.*}} target("aarch64.svcount") @test_reinterpret_c
// CIR: call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt(<vscale x 16 x i1> %{{.*}})
// OGCG-LABEL: define{{.*}} target("aarch64.svcount") @test_reinterpret_c
// OGCG: call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt
