// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve -fclangir \
// RUN:   -emit-cir %s -o %t.cir -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll -disable-llvm-passes \
// RUN:   -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

#include <arm_sve.h>

svbool_t test_svptrue_b8(void) {
  return svptrue_b8();
}

svbool_t test_svptrue_b16(void) {
  return svptrue_b16();
}

svbool_t test_svptrue_b32(void) {
  return svptrue_b32();
}

svbool_t test_svptrue_b64(void) {
  return svptrue_b64();
}

svbool_t test_svptrue_pat_b64(void) {
  return svptrue_pat_b64(SV_VL2);
}

// CIR-LABEL: cir.func {{.*}} @test_svptrue_b8() -> !cir.vector<[16] x !cir.bool>
// CIR: cir.call_llvm_intrinsic "aarch64.sve.ptrue" %{{.*}} : (!s32i) -> !cir.vector<[16] x !cir.bool>
// CIR-NOT: aarch64.sve.convert.to.svbool
// CIR: cir.return

// CIR-LABEL: cir.func {{.*}} @test_svptrue_b16() -> !cir.vector<[16] x !cir.bool>
// CIR: %[[B16:.*]] = cir.call_llvm_intrinsic "aarch64.sve.ptrue" %{{.*}} : (!s32i) -> !cir.vector<[8] x !cir.bool>
// CIR: cir.call_llvm_intrinsic "aarch64.sve.convert.to.svbool" %[[B16]] : (!cir.vector<[8] x !cir.bool>) -> !cir.vector<[16] x !cir.bool>
// CIR: cir.return

// CIR-LABEL: cir.func {{.*}} @test_svptrue_b32() -> !cir.vector<[16] x !cir.bool>
// CIR: %[[B32:.*]] = cir.call_llvm_intrinsic "aarch64.sve.ptrue" %{{.*}} : (!s32i) -> !cir.vector<[4] x !cir.bool>
// CIR: cir.call_llvm_intrinsic "aarch64.sve.convert.to.svbool" %[[B32]] : (!cir.vector<[4] x !cir.bool>) -> !cir.vector<[16] x !cir.bool>
// CIR: cir.return

// CIR-LABEL: cir.func {{.*}} @test_svptrue_b64() -> !cir.vector<[16] x !cir.bool>
// CIR: %[[B64:.*]] = cir.call_llvm_intrinsic "aarch64.sve.ptrue" %{{.*}} : (!s32i) -> !cir.vector<[2] x !cir.bool>
// CIR: cir.call_llvm_intrinsic "aarch64.sve.convert.to.svbool" %[[B64]] : (!cir.vector<[2] x !cir.bool>) -> !cir.vector<[16] x !cir.bool>
// CIR: cir.return

// CIR-LABEL: cir.func {{.*}} @test_svptrue_pat_b64() -> !cir.vector<[16] x !cir.bool>
// CIR: %[[PAT64:.*]] = cir.call_llvm_intrinsic "aarch64.sve.ptrue" %{{.*}} : (!u32i) -> !cir.vector<[2] x !cir.bool>
// CIR: cir.call_llvm_intrinsic "aarch64.sve.convert.to.svbool" %[[PAT64]] : (!cir.vector<[2] x !cir.bool>) -> !cir.vector<[16] x !cir.bool>
// CIR: cir.return

// LLVM-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b8()
// LLVM: %[[B8:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.ptrue.nxv16i1(i32 31)
// LLVM-NOT: @llvm.aarch64.sve.convert.to.svbool
// LLVM: ret <vscale x 16 x i1> %[[B8]]

// LLVM-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b16()
// LLVM: %[[B16:.*]] = call <vscale x 8 x i1> @llvm.aarch64.sve.ptrue.nxv8i1(i32 31)
// LLVM: %[[B16SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1(<vscale x 8 x i1> %[[B16]])
// LLVM: ret <vscale x 16 x i1> %[[B16SVBOOL]]

// LLVM-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b32()
// LLVM: %[[B32:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.ptrue.nxv4i1(i32 31)
// LLVM: %[[B32SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1(<vscale x 4 x i1> %[[B32]])
// LLVM: ret <vscale x 16 x i1> %[[B32SVBOOL]]

// LLVM-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b64()
// LLVM: %[[B64:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.ptrue.nxv2i1(i32 31)
// LLVM: %[[B64SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1(<vscale x 2 x i1> %[[B64]])
// LLVM: ret <vscale x 16 x i1> %[[B64SVBOOL]]

// LLVM-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_pat_b64()
// LLVM: %[[PAT64:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.ptrue.nxv2i1(i32 2)
// LLVM: %[[PAT64SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1(<vscale x 2 x i1> %[[PAT64]])
// LLVM: ret <vscale x 16 x i1> %[[PAT64SVBOOL]]

// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b8()
// OGCG: %[[B8:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.ptrue.nxv16i1(i32 31)
// OGCG-NOT: @llvm.aarch64.sve.convert.to.svbool
// OGCG: ret <vscale x 16 x i1> %[[B8]]

// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b16()
// OGCG: %[[B16:.*]] = call <vscale x 8 x i1> @llvm.aarch64.sve.ptrue.nxv8i1(i32 31)
// OGCG: %[[B16SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv8i1(<vscale x 8 x i1> %[[B16]])
// OGCG: ret <vscale x 16 x i1> %[[B16SVBOOL]]

// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b32()
// OGCG: %[[B32:.*]] = call <vscale x 4 x i1> @llvm.aarch64.sve.ptrue.nxv4i1(i32 31)
// OGCG: %[[B32SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv4i1(<vscale x 4 x i1> %[[B32]])
// OGCG: ret <vscale x 16 x i1> %[[B32SVBOOL]]

// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_b64()
// OGCG: %[[B64:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.ptrue.nxv2i1(i32 31)
// OGCG: %[[B64SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1(<vscale x 2 x i1> %[[B64]])
// OGCG: ret <vscale x 16 x i1> %[[B64SVBOOL]]

// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_svptrue_pat_b64()
// OGCG: %[[PAT64:.*]] = call <vscale x 2 x i1> @llvm.aarch64.sve.ptrue.nxv2i1(i32 2)
// OGCG: %[[PAT64SVBOOL:.*]] = call <vscale x 16 x i1> @llvm.aarch64.sve.convert.to.svbool.nxv2i1(<vscale x 2 x i1> %[[PAT64]])
// OGCG: ret <vscale x 16 x i1> %[[PAT64SVBOOL]]
