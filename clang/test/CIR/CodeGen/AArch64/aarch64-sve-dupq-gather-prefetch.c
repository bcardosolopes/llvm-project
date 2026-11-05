// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the svdupq predicate builders (svdupq_b*), the
// svdupq data builders (svdupq_n_*), and the SVE gather-prefetch family
// (svprf{b,h,w,d}_gather_*) to the same @llvm.aarch64.sve.* intrinsics as
// classic CodeGen.

#include <arm_sve.h>

// svdupq_b8: 16 booleans -> 128-bit i8 vector, dupq.lane broadcast, then an
// all-true ptrue + cmpne.wide against a <vscale x 2 x i64> zero. The b8 result
// already has 16 lanes so there is no convert.to.svbool.
svbool_t test_dupq_b8() {
  return svdupq_b8(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b8
// CIR: %[[V8:.*]] = call <vscale x 16 x i8> @llvm.vector.insert.nxv16i8.v16i8(<vscale x 16 x i8> poison, <16 x i8> <i8 1, i8 0, i8 1, i8 0, i8 1, i8 0, i8 1, i8 0, i8 1, i8 0, i8 1, i8 0, i8 1, i8 0, i8 1, i8 0>, i64 0)
// CIR: %[[D8:.*]] = call <vscale x 16 x i8> @llvm.aarch64.sve.dupq.lane.nxv16i8(<vscale x 16 x i8> %[[V8]], i64 0)
// CIR: %[[P8:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.ptrue.nxv16i1(i32 31)
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.cmpne.wide.nxv16i8(<vscale x 16 x i1> %[[P8]], <vscale x 16 x i8> %[[D8]], <vscale x 2 x i64> zeroinitializer)
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b8
// OGCG: call <vscale x 16 x i8> @llvm.aarch64.sve.dupq.lane.nxv16i8
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.ptrue.nxv16i1(i32 31)
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.cmpne.wide.nxv16i8(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i8> %{{.*}}, <vscale x 2 x i64> zeroinitializer)

// svdupq_b16: 8 booleans -> nxv8i16, cmpne.wide, then convert.to.svbool to
// widen the narrow nxv8i1 predicate back to svbool_t.
svbool_t test_dupq_b16() {
  return svdupq_b16(1, 0, 1, 0, 1, 0, 1, 0);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b16
// CIR: call <vscale x 8 x i16> @llvm.aarch64.sve.dupq.lane.nxv8i16
// CIR: %[[P16:.*]] = call <vscale x 8 x i1> @llvm.aarch64.sve.ptrue.nxv8i1(i32 31)
// CIR: %[[C16:.*]] = call <vscale x 8 x i1> @llvm.aarch64.sve.cmpne.wide.nxv8i16(<vscale x 8 x i1> %[[P16]], <vscale x 8 x i16> %{{.*}}, <vscale x 2 x i64> zeroinitializer)
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1(<vscale x 8 x i1> %[[C16]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b16
// OGCG: call <vscale x 8 x i1> @llvm.aarch64.sve.cmpne.wide.nxv8i16
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1

// svdupq_b32.
svbool_t test_dupq_b32() {
  return svdupq_b32(1, 0, 1, 0);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b32
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.dupq.lane.nxv4i32
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.ptrue.nxv4i1(i32 31)
// CIR: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpne.wide.nxv4i32(<vscale x 4 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 2 x i64> zeroinitializer)
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b32
// OGCG: call <vscale x 4 x i1> @llvm.aarch64.sve.cmpne.wide.nxv4i32
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1

// svdupq_b64: 2 booleans -> nxv2i64. This is the only predicate form that uses
// the non-wide cmpne (its third operand is still the nxv2i64 zero).
svbool_t test_dupq_b64() {
  return svdupq_b64(1, 0);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b64
// CIR: call <vscale x 2 x i64> @llvm.aarch64.sve.dupq.lane.nxv2i64
// CIR: %[[P64:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.ptrue.nxv2i1(i32 31)
// CIR: %[[C64:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.cmpne.nxv2i64(<vscale x 2 x i1> %[[P64]], <vscale x 2 x i64> %{{.*}}, <vscale x 2 x i64> zeroinitializer)
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1(<vscale x 2 x i1> %[[C64]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dupq_b64
// OGCG: call <vscale x 2 x i1> @llvm.aarch64.sve.cmpne.nxv2i64
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1

// svdupq_n_s32 (data form, constant operands): materialize the 128-bit constant
// vector, insert into the low quadword, and dupq.lane -- no predicate machinery.
svint32_t test_dupq_n_s32_const() {
  return svdupq_n_s32(1, 2, 3, 4);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_dupq_n_s32_const
// CIR: %[[VS:.*]] = call <vscale x 4 x i32> @llvm.vector.insert.nxv4i32.v4i32(<vscale x 4 x i32> poison, <4 x i32> <i32 1, i32 2, i32 3, i32 4>, i64 0)
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.dupq.lane.nxv4i32(<vscale x 4 x i32> %[[VS]], i64 0)
// CIR-NOT: cmpne
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_dupq_n_s32_const
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.dupq.lane.nxv4i32

// svdupq_n_f32 (data form, float constants).
svfloat32_t test_dupq_n_f32_const() {
  return svdupq_n_f32(1.0f, 2.0f, 3.0f, 4.0f);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x float> @test_dupq_n_f32_const
// CIR: %[[VF:.*]] = call <vscale x 4 x float> @llvm.vector.insert.nxv4f32.v4f32(<vscale x 4 x float> poison, <4 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00, float 4.000000e+00>, i64 0)
// CIR: call <vscale x 4 x float> @llvm.aarch64.sve.dupq.lane.nxv4f32(<vscale x 4 x float> %[[VF]], i64 0)
// OGCG-LABEL: define{{.*}} <vscale x 4 x float> @test_dupq_n_f32_const
// OGCG: call <vscale x 4 x float> @llvm.aarch64.sve.dupq.lane.nxv4f32

// svdupq_n_s32 (data form, runtime operands): insertelement chain feeds the
// same vector.insert + dupq.lane.
svint32_t test_dupq_n_s32(int a, int b, int c, int d) {
  return svdupq_n_s32(a, b, c, d);
}
// CIR-LABEL: define{{.*}} <vscale x 4 x i32> @test_dupq_n_s32
// CIR: insertelement <4 x i32> poison, i32 %{{.*}}, i64 0
// CIR: %[[VR:.*]] = call <vscale x 4 x i32> @llvm.vector.insert.nxv4i32.v4i32(<vscale x 4 x i32> poison, <4 x i32> %{{.*}}, i64 0)
// CIR: call <vscale x 4 x i32> @llvm.aarch64.sve.dupq.lane.nxv4i32(<vscale x 4 x i32> %[[VR]], i64 0)
// OGCG-LABEL: define{{.*}} <vscale x 4 x i32> @test_dupq_n_s32
// OGCG: call <vscale x 4 x i32> @llvm.aarch64.sve.dupq.lane.nxv4i32

// Gather-prefetch: vector base, no offset (svprfb_gather_u64base). The svbool_t
// predicate is narrowed to nxv2i1 and the intrinsic is overloaded on the base
// vector type; the omitted offset is passed as i64 0.
void test_prfb_gather_u64base(svbool_t pg, svuint64_t bases) {
  svprfb_gather_u64base(pg, bases, SV_PLDL1KEEP);
}
// CIR-LABEL: define{{.*}} void @test_prfb_gather_u64base
// CIR: %[[PB:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %{{.*}})
// CIR: call void @llvm.aarch64.sve.prfb.gather.scalar.offset.nxv2i64(<vscale x 2 x i1> %[[PB]], <vscale x 2 x i64> %{{.*}}, i64 0, i32 0)
// OGCG-LABEL: define{{.*}} void @test_prfb_gather_u64base
// OGCG: call void @llvm.aarch64.sve.prfb.gather.scalar.offset.nxv2i64(<vscale x 2 x i1> %{{.*}}, <vscale x 2 x i64> %{{.*}}, i64 0, i32 0)

// Gather-prefetch: scalar base + vector of indices (svprfd_gather_s64index).
// This flows straight to prfd.gather.index overloaded on the index vector; no
// index scaling and no operand reordering.
void test_prfd_gather_s64index(svbool_t pg, void const *base, svint64_t idx) {
  svprfd_gather_s64index(pg, base, idx, SV_PLDL1KEEP);
}
// CIR-LABEL: define{{.*}} void @test_prfd_gather_s64index
// CIR: %[[PD:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %{{.*}})
// CIR: call void @llvm.aarch64.sve.prfd.gather.index.nxv2i64(<vscale x 2 x i1> %[[PD]], ptr %{{.*}}, <vscale x 2 x i64> %{{.*}}, i32 0)
// OGCG-LABEL: define{{.*}} void @test_prfd_gather_s64index
// OGCG: call void @llvm.aarch64.sve.prfd.gather.index.nxv2i64(<vscale x 2 x i1> %{{.*}}, ptr %{{.*}}, <vscale x 2 x i64> %{{.*}}, i32 0)

// Gather-prefetch: vector base + scalar index (svprfw_gather_u32base_index).
// The index is scaled to a byte offset (shl by log2(4) = 2) before the
// scalar.offset intrinsic, which is overloaded on the base vector (nxv4i32).
void test_prfw_gather_u32base_index(svbool_t pg, svuint32_t bases, long idx) {
  svprfw_gather_u32base_index(pg, bases, idx, SV_PLDL1KEEP);
}
// CIR-LABEL: define{{.*}} void @test_prfw_gather_u32base_index
// CIR: %[[PW:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv4i1(<vscale x 16 x i1> %{{.*}})
// CIR: %[[SH:.*]] = shl i64 %{{.*}}, 2
// CIR: call void @llvm.aarch64.sve.prfw.gather.scalar.offset.nxv4i32(<vscale x 4 x i1> %[[PW]], <vscale x 4 x i32> %{{.*}}, i64 %[[SH]], i32 0)
// OGCG-LABEL: define{{.*}} void @test_prfw_gather_u32base_index
// OGCG: %[[SHO:.*]] = shl i64 %{{.*}}, 2
// OGCG: call void @llvm.aarch64.sve.prfw.gather.scalar.offset.nxv4i32(<vscale x 4 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}}, i64 %[[SHO]], i32 0)
