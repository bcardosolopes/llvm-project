// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve \
// RUN:   -target-feature +sve2 -target-feature +sve2p1 -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll -disable-llvm-passes \
// RUN:   -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve \
// RUN:   -target-feature +sve2 -target-feature +sve2p1 -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes \
// RUN:   -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

#include <arm_sve.h>

svbool_t test_svpfalse_b(void) {
  return svpfalse_b();
}

svcount_t test_svpfalse_c(void) {
  return svpfalse_c();
}

// CIR-LABEL: define{{.*}} <vscale x 16 x i1> @test_svpfalse_b()
// CIR: ret <vscale x 16 x i1> zeroinitializer

// CIR-LABEL: define{{.*}} target("aarch64.svcount") @test_svpfalse_c()
// CIR: %[[PFALSE:.*]] = call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt(<vscale x 16 x i1> zeroinitializer)
// CIR: ret target("aarch64.svcount") %[[PFALSE]]

// OGCG-LABEL: define{{.*}} <vscale x 16 x i1> @test_svpfalse_b()
// OGCG: ret <vscale x 16 x i1> zeroinitializer

// OGCG-LABEL: define{{.*}} target("aarch64.svcount") @test_svpfalse_c()
// OGCG: %[[PFALSE:.*]] = call target("aarch64.svcount") @llvm.aarch64.sve.convert.from.svbool.taarch64.svcountt(<vscale x 16 x i1> zeroinitializer)
// OGCG: ret target("aarch64.svcount") %[[PFALSE]]
