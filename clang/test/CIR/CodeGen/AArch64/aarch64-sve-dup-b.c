// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +sve2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the svdup_n_b* predicate-broadcast builders (a
// single bool splatted into a governing predicate, distinct from the svdupq_n_b*
// quadword builders) to the same @llvm.aarch64.sve.* sequence as classic
// CodeGen: cmpne against 0, an insertelement + zero-mask shufflevector splat of
// the i1 across a narrow predicate vector, then convert.to.svbool to widen back
// to svbool_t (a no-op for b8, which already has 16 lanes).

#include <arm_sve.h>

// svdup_n_b8: the splat already has 16 lanes, so there is no convert.to.svbool.
svbool_t test_dup_n_b8(bool b) {
  return svdup_n_b8(b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b8
// CIR: %[[C8:.*]] = icmp ne i1 %{{.*}}, false
// CIR: %[[I8:.*]] = insertelement <vscale x 16 x i1> poison, i1 %[[C8]], i64 0
// CIR: shufflevector <vscale x 16 x i1> %[[I8]], <vscale x 16 x i1> poison, <vscale x 16 x i32> zeroinitializer
// CIR-NOT: convert.to.svbool
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b8
// OGCG: %[[OC8:.*]] = icmp ne i1 %{{.*}}, false
// OGCG: %[[OI8:.*]] = insertelement <vscale x 16 x i1> poison, i1 %[[OC8]], i64 0
// OGCG: shufflevector <vscale x 16 x i1> %[[OI8]], <vscale x 16 x i1> poison, <vscale x 16 x i32> zeroinitializer
// OGCG-NOT: convert.to.svbool

// svdup_n_b16: nxv8i1 splat, then convert.to.svbool widens to svbool_t.
svbool_t test_dup_n_b16(bool b) {
  return svdup_n_b16(b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b16
// CIR: %[[C16:.*]] = icmp ne i1 %{{.*}}, false
// CIR: %[[I16:.*]] = insertelement <vscale x 8 x i1> poison, i1 %[[C16]], i64 0
// CIR: %[[S16:.*]] = shufflevector <vscale x 8 x i1> %[[I16]], <vscale x 8 x i1> poison, <vscale x 8 x i32> zeroinitializer
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1(<vscale x 8 x i1> %[[S16]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b16
// OGCG: shufflevector <vscale x 8 x i1>
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1

// svdup_n_b32: nxv4i1 splat + convert.to.svbool.
svbool_t test_dup_n_b32(bool b) {
  return svdup_n_b32(b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b32
// CIR: %[[S32:.*]] = shufflevector <vscale x 4 x i1> %{{.*}}, <vscale x 4 x i1> poison, <vscale x 4 x i32> zeroinitializer
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1(<vscale x 4 x i1> %[[S32]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b32
// OGCG: shufflevector <vscale x 4 x i1>
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1

// svdup_n_b64: nxv2i1 splat + convert.to.svbool.
svbool_t test_dup_n_b64(bool b) {
  return svdup_n_b64(b);
}
// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b64
// CIR: %[[S64:.*]] = shufflevector <vscale x 2 x i1> %{{.*}}, <vscale x 2 x i1> poison, <vscale x 2 x i32> zeroinitializer
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1(<vscale x 2 x i1> %[[S64]])
// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_dup_n_b64
// OGCG: shufflevector <vscale x 2 x i1>
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1
