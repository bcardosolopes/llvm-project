// RUN: %clang_cc1 -triple aarch64-eabi -target-feature +gcs -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-eabi -target-feature +gcs -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Test that __gcspr() (via __builtin_arm_rsr64) lowers to
// llvm.read_volatile_register, matching OGCG behavior.

#include <arm_acle.h>

void *test_gcspr() {
  return __gcspr();
}

// CIR: define {{.*}}ptr @test_gcspr()
// CIR:   call i64 @llvm.read_volatile_register.i64(metadata !{{.*}})

// OGCG: define {{.*}}ptr @test_gcspr()
// OGCG:   call i64 @llvm.read_volatile_register.i64(metadata !{{.*}})
