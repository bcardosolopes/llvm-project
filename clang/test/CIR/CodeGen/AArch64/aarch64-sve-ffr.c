// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the SVE FFR builtins to the same
// @llvm.aarch64.sve.rdffr/.z/wrffr intrinsics as classic CodeGen. These route
// through the generic intrinsic-signature decoder (decodeFixedType), whose
// IITDescriptor::Vector case must handle the scalable nxv16i1 svbool_t result
// without aborting. Tested at the raw __builtin_sve_* level to avoid the
// generated arm_sve.h header dependency; the ACLE prototypes are declared here
// via __clang_arm_builtin_alias, exactly as arm_sve.h does.

typedef __SVBool_t svbool_t;

__attribute__((__clang_arm_builtin_alias(__builtin_sve_svrdffr)))
svbool_t svrdffr(void);
__attribute__((__clang_arm_builtin_alias(__builtin_sve_svrdffr_z)))
svbool_t svrdffr_z(svbool_t);
__attribute__((__clang_arm_builtin_alias(__builtin_sve_svwrffr)))
void svwrffr(svbool_t);

// svrdffr(): reads FFR into an all-lanes svbool_t.
svbool_t test_rdffr(void) {
  return svrdffr();
}
// CIR-LABEL: @test_rdffr
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.rdffr()
// OGCG-LABEL: @test_rdffr
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.rdffr()

// svrdffr_z(pg): governed read of FFR.
svbool_t test_rdffr_z(svbool_t pg) {
  return svrdffr_z(pg);
}
// CIR-LABEL: @test_rdffr_z
// CIR: call <vscale x 16 x i1> @llvm.aarch64.sve.rdffr.z(<vscale x 16 x i1>
// OGCG-LABEL: @test_rdffr_z
// OGCG: call <vscale x 16 x i1> @llvm.aarch64.sve.rdffr.z(<vscale x 16 x i1>

// svwrffr(pg): writes FFR from an svbool_t.
void test_wrffr(svbool_t pg) {
  svwrffr(pg);
}
// CIR-LABEL: @test_wrffr
// CIR: call void @llvm.aarch64.sve.wrffr(<vscale x 16 x i1>
// OGCG-LABEL: @test_wrffr
// OGCG: call void @llvm.aarch64.sve.wrffr(<vscale x 16 x i1>
