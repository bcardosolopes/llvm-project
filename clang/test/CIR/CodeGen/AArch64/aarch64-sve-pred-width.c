// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR narrows an SVE builtin's governing predicate to the
// element width encoded by the builtin's TypeFlags (mirroring classic
// CodeGenFunction::EmitAArch64SVEBuiltinExpr, which casts every predicate
// operand to getSVEType(TypeFlags) via EmitSVEPredicateCast) rather than
// inferring it from a data operand or the result type. Three families exercise
// the failure modes that inference gets wrong: svcntp (scalar result), svpnext/
// svunpk (predicate-only), and svcvt with mismatched element widths.

#include <arm_sve.h>

// --- svcntp: scalar result, both operands are svbool_t. Inference left the
// predicates at nxv16i1 and silently called cntp.nxv16i1 for every width, a
// MISCOMPILE that counted every byte lane. b8 needs no narrowing; b16/32/64
// must narrow BOTH operands to nxv8/4/2i1.

uint64_t test_cntp_b8(svbool_t pg, svbool_t op) { return svcntp_b8(pg, op); }
// CIR-LABEL: define{{.*}} i64 @test_cntp_b8
// CIR-NOT:   convert.from.svbool
// CIR:       call i64 @llvm.aarch64.sve.cntp.nxv16i1
// OGCG-LABEL: define{{.*}} i64 @test_cntp_b8
// OGCG-NOT:   convert.from.svbool
// OGCG:       call i64 @llvm.aarch64.sve.cntp.nxv16i1

uint64_t test_cntp_b16(svbool_t pg, svbool_t op) { return svcntp_b16(pg, op); }
// CIR-LABEL: define{{.*}} i64 @test_cntp_b16
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// CIR:       call i64 @llvm.aarch64.sve.cntp.nxv8i1
// OGCG-LABEL: define{{.*}} i64 @test_cntp_b16
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// OGCG:       call i64 @llvm.aarch64.sve.cntp.nxv8i1

uint64_t test_cntp_b32(svbool_t pg, svbool_t op) { return svcntp_b32(pg, op); }
// CIR-LABEL: define{{.*}} i64 @test_cntp_b32
// CIR:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR:       call i64 @llvm.aarch64.sve.cntp.nxv4i1
// OGCG-LABEL: define{{.*}} i64 @test_cntp_b32
// OGCG:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG:       call i64 @llvm.aarch64.sve.cntp.nxv4i1

uint64_t test_cntp_b64(svbool_t pg, svbool_t op) { return svcntp_b64(pg, op); }
// CIR-LABEL: define{{.*}} i64 @test_cntp_b64
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR:       call i64 @llvm.aarch64.sve.cntp.nxv2i1
// OGCG-LABEL: define{{.*}} i64 @test_cntp_b64
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG:       call i64 @llvm.aarch64.sve.cntp.nxv2i1

// --- svpnext: predicate-only. Narrow operands, then widen the narrow result
// back to svbool_t via convert.to.svbool. b8 needs neither.

svbool_t test_pnext_b8(svbool_t pg, svbool_t op) { return svpnext_b8(pg, op); }
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b8
// CIR:       call <vscale x 16 x i1> @llvm.aarch64.sve.pnext.nxv16i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b8
// OGCG:       call <vscale x 16 x i1> @llvm.aarch64.sve.pnext.nxv16i1

svbool_t test_pnext_b16(svbool_t pg, svbool_t op) { return svpnext_b16(pg, op); }
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b16
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.pnext.nxv8i1
// CIR:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b16
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.pnext.nxv8i1
// OGCG:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1

svbool_t test_pnext_b32(svbool_t pg, svbool_t op) { return svpnext_b32(pg, op); }
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b32
// CIR:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR:       call <vscale x 4 x i1> @llvm.aarch64.sve.pnext.nxv4i1
// CIR:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b32
// OGCG:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG:       call <vscale x 4 x i1> @llvm.aarch64.sve.pnext.nxv4i1
// OGCG:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1

svbool_t test_pnext_b64(svbool_t pg, svbool_t op) { return svpnext_b64(pg, op); }
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b64
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.pnext.nxv2i1
// CIR:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_pnext_b64
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.pnext.nxv2i1
// OGCG:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1

// --- svunpklo/hi_b: the punpk intrinsic takes svbool_t (no narrowing) but
// RETURNS a half-width predicate (nxv8i1), which must be widened back.

svbool_t test_unpklo_b(svbool_t op) { return svunpklo_b(op); }
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_unpklo_b
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.punpklo.nxv16i1
// CIR:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_unpklo_b
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.punpklo.nxv16i1
// OGCG:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1

svbool_t test_unpkhi_b(svbool_t op) { return svunpkhi_b(op); }
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_unpkhi_b
// CIR:       call <vscale x 8 x i1> @llvm.aarch64.sve.punpkhi.nxv16i1
// CIR:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_unpkhi_b
// OGCG:       call <vscale x 8 x i1> @llvm.aarch64.sve.punpkhi.nxv16i1
// OGCG:       call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1

// --- svcvt: governing predicate follows the wider-element (fewer-lane)
// operand, i.e. min(srcLanes,dstLanes). When the result has MORE lanes than the
// source, inferring from the result overshoots the predicate width and the fcvt
// intrinsic overload mismatches. svcvt_f32_f64 wants nxv2i1, not nxv4i1.

svfloat32_t test_cvt_f32_f64_z(svbool_t p, svfloat64_t a) { return svcvt_f32_f64_z(p, a); }
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_cvt_f32_f64_z
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR:       call <vscale x 4 x float> @llvm.aarch64.sve.fcvt.f32f64(<vscale x 4 x float> zeroinitializer, <vscale x 2 x i1> {{.*}}, <vscale x 2 x double>
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_cvt_f32_f64_z
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG:       call <vscale x 4 x float> @llvm.aarch64.sve.fcvt.f32f64(<vscale x 4 x float> zeroinitializer, <vscale x 2 x i1> {{.*}}, <vscale x 2 x double>

svfloat16_t test_cvt_f16_f32_z(svbool_t p, svfloat32_t a) { return svcvt_f16_f32_z(p, a); }
// CIR-LABEL: define{{.*}} <vscale x 8 x half> @test_cvt_f16_f32_z
// CIR:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// CIR:       call <vscale x 8 x half> @llvm.aarch64.sve.fcvt.f16f32(<vscale x 8 x half> zeroinitializer, <vscale x 4 x i1> {{.*}}, <vscale x 4 x float>
// OGCG-LABEL: define{{.*}} <vscale x 8 x half> @test_cvt_f16_f32_z
// OGCG:       call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1
// OGCG:       call <vscale x 8 x half> @llvm.aarch64.sve.fcvt.f16f32(<vscale x 8 x half> zeroinitializer, <vscale x 4 x i1> {{.*}}, <vscale x 4 x float>

svint32_t test_cvt_s32_f64_z(svbool_t p, svfloat64_t a) { return svcvt_s32_f64_z(p, a); }
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_cvt_s32_f64_z
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR:       call <vscale x 4 x i32> @llvm.aarch64.sve.fcvtzs.i32f64(<vscale x 4 x i32> zeroinitializer, <vscale x 2 x i1> {{.*}}, <vscale x 2 x double>
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_cvt_s32_f64_z
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG:       call <vscale x 4 x i32> @llvm.aarch64.sve.fcvtzs.i32f64(<vscale x 4 x i32> zeroinitializer, <vscale x 2 x i1> {{.*}}, <vscale x 2 x double>

// Control: result has <= source lanes, so the predicate already matched even
// under the old inference (svcvt_f64_f32 wants nxv2i1 from the f32 result width
// too). Confirms the fix does not perturb the already-correct direction.
svfloat64_t test_cvt_f64_f32_z(svbool_t p, svfloat32_t a) { return svcvt_f64_f32_z(p, a); }
// CIR-LABEL: define{{.*}} <vscale x 2 x double> @test_cvt_f64_f32_z
// CIR:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// CIR:       call <vscale x 2 x double> @llvm.aarch64.sve.fcvt.f64f32(<vscale x 2 x double> zeroinitializer, <vscale x 2 x i1> {{.*}}, <vscale x 4 x float>
// OGCG-LABEL: define{{.*}} <vscale x 2 x double> @test_cvt_f64_f32_z
// OGCG:       call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1
// OGCG:       call <vscale x 2 x double> @llvm.aarch64.sve.fcvt.f64f32(<vscale x 2 x double> zeroinitializer, <vscale x 2 x i1> {{.*}}, <vscale x 4 x float>
