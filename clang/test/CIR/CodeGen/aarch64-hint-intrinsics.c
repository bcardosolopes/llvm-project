// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

#include <arm_acle.h>

void test_hints(void) {
  __builtin_arm_nop();
  __builtin_arm_yield();
  __builtin_arm_wfe();
  __builtin_arm_wfi();
  __builtin_arm_sev();
  __builtin_arm_sevl();
}

// CIR-LABEL: @test_hints
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()

// LLVM-LABEL: @test_hints
// LLVM: call void @llvm.aarch64.hint(i32 0)
// LLVM: call void @llvm.aarch64.hint(i32 1)
// LLVM: call void @llvm.aarch64.hint(i32 2)
// LLVM: call void @llvm.aarch64.hint(i32 3)
// LLVM: call void @llvm.aarch64.hint(i32 4)
// LLVM: call void @llvm.aarch64.hint(i32 5)

// OGCG-LABEL: @test_hints
// OGCG: call void @llvm.aarch64.hint(i32 0)
// OGCG: call void @llvm.aarch64.hint(i32 1)
// OGCG: call void @llvm.aarch64.hint(i32 2)
// OGCG: call void @llvm.aarch64.hint(i32 3)
// OGCG: call void @llvm.aarch64.hint(i32 4)
// OGCG: call void @llvm.aarch64.hint(i32 5)

void test_yield_alias(void) {
  __yield();
}

// CIR-LABEL: @test_yield_alias
// CIR: cir.call_llvm_intrinsic "aarch64.hint" {{.*}} : (!s32i) -> ()

// LLVM-LABEL: @test_yield_alias
// LLVM: call void @llvm.aarch64.hint(i32 1)

// OGCG-LABEL: @test_yield_alias
// OGCG: call void @llvm.aarch64.hint(i32 1)
