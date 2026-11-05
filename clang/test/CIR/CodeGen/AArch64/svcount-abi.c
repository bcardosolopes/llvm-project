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

uint64_t test_svcntp_c8_vlx2(svcount_t pnn) {
  return svcntp_c8(pnn, 2);
}

void test_copy_cnt(svcount_t a) {
  svcount_t b = a;
}

// CIR-LABEL: define{{.*}} i64 @test_svcntp_c8_vlx2(
// CIR-SAME: target("aarch64.svcount") %{{.*}})
// CIR: %{{.*}} = call i64 @llvm.aarch64.sve.cntp.c8(target("aarch64.svcount") %{{.*}}, i32 2)

// CIR-LABEL: define{{.*}} void @test_copy_cnt(
// CIR-SAME: target("aarch64.svcount") %{{.*}})
// CIR: %{{.*}} = alloca target("aarch64.svcount"), i64 1, align 2
// CIR: %{{.*}} = alloca target("aarch64.svcount"), i64 1, align 2
// CIR: store target("aarch64.svcount") %{{.*}}, ptr %{{.*}}, align 2
// CIR: %{{.*}} = load target("aarch64.svcount"), ptr %{{.*}}, align 2
// CIR: store target("aarch64.svcount") %{{.*}}, ptr %{{.*}}, align 2
// CIR: ret void

// OGCG-LABEL: define{{.*}} i64 @test_svcntp_c8_vlx2(
// OGCG-SAME: target("aarch64.svcount") %{{.*}})
// OGCG: %{{.*}} = call i64 @llvm.aarch64.sve.cntp.c8(target("aarch64.svcount") %{{.*}}, i32 2)

// OGCG-LABEL: define{{.*}} void @test_copy_cnt(
// OGCG-SAME: target("aarch64.svcount") %{{.*}})
// OGCG: %{{.*}} = alloca target("aarch64.svcount"), align 2
// OGCG: %{{.*}} = alloca target("aarch64.svcount"), align 2
// OGCG: store target("aarch64.svcount") %{{.*}}, ptr %{{.*}}, align 2
// OGCG: %{{.*}} = load target("aarch64.svcount"), ptr %{{.*}}, align 2
// OGCG: store target("aarch64.svcount") %{{.*}}, ptr %{{.*}}, align 2
// OGCG: ret void
