// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve \
// RUN:   -fclangir -emit-llvm %s -o %t.cir.ll -disable-llvm-passes \
// RUN:   -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -target-feature +sve \
// RUN:   -emit-llvm %s -o %t.ogcg.ll -disable-llvm-passes \
// RUN:   -internal-isystem %resource_dir/include
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that SVE predicate types (svbool_t) are passed as
// <vscale x 16 x i1> and NOT coerced to i32. Also verify that
// scalable vectors do not get noundef (matching OGCG).

#include <arm_sve.h>

svint32_t sve_add(svbool_t pg, svint32_t a, svint32_t b) {
  return svadd_s32_m(pg, a, b);
}

// LLVM: define dso_local <vscale x 4 x i32> @sve_add(<vscale x 16 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})
// OGCG: define dso_local <vscale x 4 x i32> @sve_add(<vscale x 16 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})

// Verify no noundef on scalable vectors
// LLVM-NOT: noundef <vscale
// OGCG-NOT: noundef <vscale
